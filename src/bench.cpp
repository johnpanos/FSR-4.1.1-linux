// SPDX-License-Identifier: GPL-2.0-or-later
// Native Vulkan FSR 4.1.1 benchmark, adapted from bbport (see NOTICE.md).
// fsr411-bench [render WxH] [output WxH] [preset 0-4] [frames 1-600]
// BENCH_NOISE=1 selects deterministic input data shared with the DLL reference.
// BENCH_DUMP=<file> saves the last output frame as raw RGBA16F.
// BB_FSR411_DIR overrides the shader/model directory (default: assets).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "fsr411.h"

namespace {

#define CHECK(call)                                                                            \
    do {                                                                                       \
        const VkResult r_ = (call);                                                            \
        if (r_ != VK_SUCCESS) {                                                                \
            std::fprintf(stderr, "%s failed: %d\n", #call, int(r_));                           \
            std::exit(1);                                                                      \
        }                                                                                      \
    } while (0)

struct Gpu {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family = 0;
    VkPhysicalDeviceMemoryProperties memory{};
};

uint32_t MemoryType(const Gpu& gpu, uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < gpu.memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (gpu.memory.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    std::fprintf(stderr, "no memory type\n");
    std::exit(1);
}

Gpu CreateGpu() {
    Gpu gpu;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "fsr4-bench";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    CHECK(vkCreateInstance(&ici, nullptr, &gpu.instance));
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(gpu.instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    CHECK(vkEnumeratePhysicalDevices(gpu.instance, &count, devices.data()));
    if (devices.empty()) {
        std::fprintf(stderr, "no Vulkan GPU\n");
        std::exit(1);
    }
    gpu.physical = devices.at(0);
    for (const auto device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            gpu.physical = device;
            break;
        }
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu.physical, &props);
    std::printf("GPU: %s\n", props.deviceName);
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU || props.apiVersion < VK_API_VERSION_1_3) {
        std::fprintf(stderr, "a hardware Vulkan 1.3 device is required\n");
        std::exit(1);
    }
    vkGetPhysicalDeviceMemoryProperties(gpu.physical, &gpu.memory);

    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.physical, &families, nullptr);
    std::vector<VkQueueFamilyProperties> family_props(families);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.physical, &families, family_props.data());
    while (gpu.family < families && !(family_props[gpu.family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        ++gpu.family;
    }

    // Everything the device supports, as the game's device enables what FSR 4 needs.
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    // FSR 4.1.1 passes use mixed float dot products (dot2 of halves into float), as vkd3d-proton.
    VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE mixed_dot{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE};
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    f13.pNext = &derivatives;
    derivatives.pNext = &mixed_dot;
    vkGetPhysicalDeviceFeatures2(gpu.physical, &features);
    features.features.robustBufferAccess = VK_FALSE; // as the game: no robustness cost
    f13.robustImageAccess = VK_FALSE;
    std::vector<const char*> extensions{VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME};
    extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (mixed_dot.shaderMixedFloatDotProductFloat16AccFloat32) {
        extensions.push_back(VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME);
    }
    uint32_t extension_count = 0;
    CHECK(vkEnumerateDeviceExtensionProperties(gpu.physical, nullptr, &extension_count, nullptr));
    std::vector<VkExtensionProperties> available(extension_count);
    CHECK(vkEnumerateDeviceExtensionProperties(gpu.physical, nullptr, &extension_count, available.data()));
    for (const char* wanted : extensions) {
        if (std::none_of(available.begin(), available.end(), [wanted](const auto& x) {
                return std::strcmp(x.extensionName, wanted) == 0;
            })) {
            std::fprintf(stderr, "required Vulkan extension missing: %s\n", wanted);
            std::exit(1);
        }
    }
    if (!mixed_dot.shaderMixedFloatDotProductFloat16AccFloat32 || !derivatives.computeDerivativeGroupLinear ||
        !f12.shaderFloat16 || !f12.shaderInt8 || !features.features.shaderInt16 ||
        !f13.shaderIntegerDotProduct || !features.features.shaderStorageImageWriteWithoutFormat) {
        std::fprintf(stderr, "required FSR 4.1.1 shader features missing\n");
        std::exit(1);
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = gpu.family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &features;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(extensions.size());
    dci.ppEnabledExtensionNames = extensions.data();
    CHECK(vkCreateDevice(gpu.physical, &dci, nullptr, &gpu.device));
    vkGetDeviceQueue(gpu.device, gpu.family, 0, &gpu.queue);
    return gpu;
}

struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void* data = nullptr;
};

Buffer CreateHostBuffer(const Gpu& gpu, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer b;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    CHECK(vkCreateBuffer(gpu.device, &ci, nullptr, &b.buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(gpu.device, b.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemoryType(gpu, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkAllocateMemory(gpu.device, &ai, nullptr, &b.memory));
    CHECK(vkBindBufferMemory(gpu.device, b.buffer, b.memory, 0));
    CHECK(vkMapMemory(gpu.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.data));
    return b;
}

uint16_t Half(float f) {
    // Inputs in [0, 2): exact enough for test data (truncating conversion).
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    const int exp = int((x >> 23) & 0xffu) - 127 + 15;
    if (exp <= 0) {
        return uint16_t(sign);
    }
    return uint16_t(sign | (uint32_t(exp) << 10) | ((x >> 13) & 0x3ffu));
}

struct Image {
    VkImage image{};
    VkImageView view{};
    VkDeviceMemory memory{};
    uint32_t width = 0, height = 0;
};

Image CreateImage(const Gpu& gpu, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage,
                  VkImageAspectFlags aspect) {
    Image image;
    image.width = w;
    image.height = h;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    CHECK(vkCreateImage(gpu.device, &ci, nullptr, &image.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(gpu.device, image.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkAllocateMemory(gpu.device, &ai, nullptr, &image.memory));
    CHECK(vkBindImageMemory(gpu.device, image.image, image.memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, 1, 0, 1};
    CHECK(vkCreateImageView(gpu.device, &vi, nullptr, &image.view));
    return image;
}

} // namespace

int main(int argc, char** argv) {
    uint32_t rw = 1280, rh = 720, ow = 1920, oh = 1080;
    int preset = 2, frames = 8;
    bool fsr411 = true;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fsr411") == 0) {
            fsr411 = true;
            continue;
        }
        switch (positional++) {
        case 0:
            std::sscanf(argv[i], "%ux%u", &rw, &rh);
            break;
        case 1:
            std::sscanf(argv[i], "%ux%u", &ow, &oh);
            break;
        case 2:
            preset = std::atoi(argv[i]);
            break;
        case 3:
            frames = std::atoi(argv[i]);
            break;
        }
    }
    if (!rw || !rh || !ow || !oh || rw > ow || rh > oh || ow > 3840 || oh > 2160 ||
        frames < 1 || frames > 600 || preset < 0 || preset > 4) {
        std::fprintf(stderr, "usage: fsr411-bench [render WxH] [output WxH] [preset 0-4] [frames 1-600]\n");
        return 1;
    }
    setenv("BB_FSR4_PROFILE", "0", 0);
    const Gpu gpu = CreateGpu();

    const VkImageUsageFlags rw_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Image color = CreateImage(gpu, VK_FORMAT_R16G16B16A16_SFLOAT, rw, rh, rw_usage,
                              VK_IMAGE_ASPECT_COLOR_BIT);
    Image depth = CreateImage(gpu, VK_FORMAT_D32_SFLOAT, rw, rh,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
    Image motion = CreateImage(gpu, VK_FORMAT_R16G16_SFLOAT, rw, rh, rw_usage,
                               VK_IMAGE_ASPECT_COLOR_BIT);
    Image output = CreateImage(gpu, VK_FORMAT_R16G16B16A16_SFLOAT, ow, oh, rw_usage,
                               VK_IMAGE_ASPECT_COLOR_BIT);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = gpu.family;
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(gpu.device, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(gpu.device, &cai, &cmd));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(gpu.device, &fci, nullptr, &fence));

    const auto submit = [&] {
        CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        CHECK(vkQueueSubmit(gpu.queue, 1, &si, fence));
        CHECK(vkWaitForFences(gpu.device, 1, &fence, VK_TRUE, UINT64_MAX));
        CHECK(vkResetFences(gpu.device, 1, &fence));
    };
    const VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    // Inputs: a mid-grey frame, far depth, small motion; all in General.
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    const auto to_general = [&](const Image& image, VkImageAspectFlags aspect) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image.image;
        b.subresourceRange = {aspect, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    to_general(color, VK_IMAGE_ASPECT_COLOR_BIT);
    to_general(depth, VK_IMAGE_ASPECT_DEPTH_BIT);
    to_general(motion, VK_IMAGE_ASPECT_COLOR_BIT);
    to_general(output, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageSubresourceRange color_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkClearColorValue grey{{0.4f, 0.35f, 0.3f, 1.0f}}, mv{{0.25f, -0.5f, 0.0f, 0.0f}};
    vkCmdClearColorImage(cmd, color.image, VK_IMAGE_LAYOUT_GENERAL, &grey, 1, &color_range);
    vkCmdClearColorImage(cmd, motion.image, VK_IMAGE_LAYOUT_GENERAL, &mv, 1, &color_range);
    const VkClearDepthStencilValue far{0.5f, 0};
    const VkImageSubresourceRange depth_range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vkCmdClearDepthStencilImage(cmd, depth.image, VK_IMAGE_LAYOUT_GENERAL, &far, 1, &depth_range);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
    submit();

    const char* noise_env = std::getenv("BENCH_NOISE");
    if (noise_env && noise_env[0] == '1') {
        // Pseudo-random color, motion within +-2 pixels, depth; one upload.
        const VkDeviceSize color_bytes = VkDeviceSize(rw) * rh * 8, motion_bytes = VkDeviceSize(rw) * rh * 4,
                           depth_bytes = VkDeviceSize(rw) * rh * 4;
        Buffer staging = CreateHostBuffer(gpu, color_bytes + motion_bytes + depth_bytes,
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        uint32_t seed = 12345u;
        const auto next = [&] {
            seed = seed * 1664525u + 1013904223u;
            return float(seed >> 8) / float(1u << 24);
        };
        auto* bytes = static_cast<unsigned char*>(staging.data);
        auto* c16 = reinterpret_cast<uint16_t*>(bytes);
        for (VkDeviceSize i = 0; i < VkDeviceSize(rw) * rh; ++i) {
            c16[i * 4 + 0] = Half(next() * 1.5f);
            c16[i * 4 + 1] = Half(next() * 1.5f);
            c16[i * 4 + 2] = Half(next() * 1.5f);
            c16[i * 4 + 3] = Half(1.0f);
        }
        auto* m16 = reinterpret_cast<uint16_t*>(bytes + color_bytes);
        for (VkDeviceSize i = 0; i < VkDeviceSize(rw) * rh * 2; ++i) {
            const float v = next() * 4.0f - 2.0f;
            m16[i] = Half(v);
        }
        auto* d32 = reinterpret_cast<float*>(bytes + color_bytes + motion_bytes);
        for (VkDeviceSize i = 0; i < VkDeviceSize(rw) * rh; ++i) {
            d32[i] = 0.9f + next() * 0.1f;
        }
        CHECK(vkBeginCommandBuffer(cmd, &begin));
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {rw, rh, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, color.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        region.bufferOffset = color_bytes;
        vkCmdCopyBufferToImage(cmd, staging.buffer, motion.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        region.bufferOffset = color_bytes + motion_bytes;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCmdCopyBufferToImage(cmd, staging.buffer, depth.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0,
                             nullptr);
        submit();
        vkUnmapMemory(gpu.device, staging.memory);
        vkDestroyBuffer(gpu.device, staging.buffer, nullptr);
        vkFreeMemory(gpu.device, staging.memory, nullptr);
    }

    std::unique_ptr<Fsr411::Upscaler> upscaler411;
    VkQueryPool timestamps = VK_NULL_HANDLE;
    std::vector<double> gpu_times;
    float period_ns = 1.0f;
    if (fsr411) {
        const char* dir411 = std::getenv("BB_FSR411_DIR");
        upscaler411 = std::make_unique<Fsr411::Upscaler>(gpu.physical, gpu.device,
                                                         dir411 && dir411[0] ? dir411 : "assets");
        VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qci.queryCount = 2;
        CHECK(vkCreateQueryPool(gpu.device, &qci, nullptr, &timestamps));
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu.physical, &props);
        period_ns = props.limits.timestampPeriod;
        std::printf("FSR 4.1.1 replay %ux%u -> %ux%u, %d frames\n", rw, rh, ow, oh, frames);
    }
    for (int frame = 1; fsr411 && frame <= frames; ++frame) {
        CHECK(vkBeginCommandBuffer(cmd, &begin));
        vkCmdResetQueryPool(cmd, timestamps, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps, 0);
        Fsr411::Frame f;
        f.cmdbuf = cmd;
        f.color = {color.image, color.view, rw, rh};
        f.depth = {depth.image, depth.view, rw, rh};
        f.motion = {motion.image, motion.view, rw, rh};
        f.output = {output.image, output.view, ow, oh};
        f.render_width = rw;
        f.render_height = rh;
        f.ultra_performance = preset >= 4;
        f.jitter[0] = 0.25f * float((frame - 1) % 4) - 0.375f; // as fsr4cap
        f.jitter[1] = 0.125f;
        f.sharpen = true;
        f.sharpness = 0.5f;
        f.reset = frame == 1;
        f.auto_exposure = true;
        if (!upscaler411->Record(f)) {
            std::fprintf(stderr, "FSR 4.1.1: %s\n", upscaler411->Error().c_str());
            return 1;
        }
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps, 1);
        submit();
        uint64_t ts[2];
        CHECK(vkGetQueryPoolResults(gpu.device, timestamps, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT));
        gpu_times.push_back(double(ts[1] - ts[0]) * period_ns * 1e-6);
    }
    const size_t warmup = std::min<size_t>(32, gpu_times.size() / 2);
    std::vector<double> scored(gpu_times.begin() + warmup, gpu_times.end());
    std::sort(scored.begin(), scored.end());
    std::printf("GPU upscale: median %.3f ms, p95 %.3f ms (%zu frames, %zu warmup, %s)\n",
                scored[scored.size()/2], scored[std::min(scored.size()-1, size_t(scored.size()*0.95))],
                scored.size(), warmup, upscaler411->Describe().c_str());
    CHECK(vkDeviceWaitIdle(gpu.device));
    if (const char* dump = std::getenv("BENCH_DUMP")) {
        const VkDeviceSize bytes = VkDeviceSize(ow) * oh * 8;
        Buffer readback = CreateHostBuffer(gpu, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        CHECK(vkBeginCommandBuffer(cmd, &begin));
        VkMemoryBarrier rb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        rb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &rb, 0, nullptr, 0, nullptr);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {ow, oh, 1};
        vkCmdCopyImageToBuffer(cmd, output.image, VK_IMAGE_LAYOUT_GENERAL, readback.buffer, 1,
                               &region);
        submit();
        if (FILE* f = std::fopen(dump, "wb")) {
            std::fwrite(readback.data, 1, size_t(bytes), f);
            std::fclose(f);
        } else {
            std::fprintf(stderr, "cannot open output %s\n", dump);
            return 1;
        }
        vkUnmapMemory(gpu.device, readback.memory);
        vkDestroyBuffer(gpu.device, readback.buffer, nullptr);
        vkFreeMemory(gpu.device, readback.memory, nullptr);
    }
    upscaler411.reset();
    vkDestroyQueryPool(gpu.device, timestamps, nullptr);
    vkDestroyFence(gpu.device, fence, nullptr);
    vkDestroyCommandPool(gpu.device, pool, nullptr);
    for (auto* image : {&color, &depth, &motion, &output}) {
        vkDestroyImageView(gpu.device, image->view, nullptr);
        vkDestroyImage(gpu.device, image->image, nullptr);
        vkFreeMemory(gpu.device, image->memory, nullptr);
    }
    vkDestroyDevice(gpu.device, nullptr);
    vkDestroyInstance(gpu.instance, nullptr);
    return 0;
}
