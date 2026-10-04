#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Rewrites a shader's VK_VALVE_shader_mixed_float_dot_product instructions into core SPIR-V,
# for drivers without that extension (NVIDIA among them).
#
#   portable_dot.py <in.spv> <out.spv>
#
# Each OpFDot2MixAcc32VALVE (dot of two half2 into a float accumulator, HLSL dot2add) becomes
# two exact half -> float conversions and two GLSL.std.450 Fma:
#   r = fma(a.x, b.x, fma(a.y, b.y, acc))
# A product of two halves is exact in float, so this differs from the fused instruction only in
# the rounding of the inner sum. The capability and extension are removed. Any other VALVE
# instruction (16-bit or bfloat16 accumulation, Dot4) is an error, not a silent passthrough.
import struct, sys

OP_EXTENSION, OP_EXT_INST_IMPORT, OP_EXT_INST, OP_MEMORY_MODEL = 10, 11, 12, 14
OP_CAPABILITY, OP_TYPE_FLOAT, OP_TYPE_VECTOR = 17, 22, 23
OP_COMPOSITE_EXTRACT, OP_FCONVERT = 81, 115
OP_FDOT2_MIX_ACC32_VALVE, OP_FDOT2_MIX_ACC16_VALVE, OP_FDOT4_MIX_ACC32_VALVE = 6916, 6917, 6918
CAP_VALVE_FIRST, CAP_VALVE_LAST = 6912, 6915
CAP_DOT_F16_ACC_F32_VALVE = 6912
GLSL_FMA = 50
EXTENSION = 'SPV_VALVE_mixed_float_dot_product'


def string_words(text):
    data = text.encode() + b'\0'
    data += b'\0' * (-len(data) % 4)
    return list(struct.unpack('<%dI' % (len(data) // 4), data))


def words_string(words):
    return struct.pack('<%dI' % len(words), *words).split(b'\0', 1)[0].decode()


def inst(op, *operands):
    return [((len(operands) + 1) << 16) | op, *operands]


def rewrite(data):
    if len(data) % 4:
        raise ValueError('not a SPIR-V module: size is not a multiple of 4')
    words = list(struct.unpack('<%dI' % (len(data) // 4), data))
    if words[0] != 0x07230203:
        raise ValueError('not a little-endian SPIR-V module')
    header, body = words[:5], words[5:]
    insts, i = [], 0
    while i < len(body):
        count = body[i] >> 16
        if count == 0 or i + count > len(body):
            raise ValueError('truncated instruction at word %d' % (i + 5))
        insts.append(body[i:i + count])
        i += count

    bound = header[3]

    def fresh():
        nonlocal bound
        bound += 1
        return bound - 1

    float32 = vec2 = glsl = None
    for w in insts:
        op = w[0] & 0xffff
        if op == OP_TYPE_FLOAT and w[2] == 32 and len(w) == 3:
            float32 = w[1]
        elif op == OP_EXT_INST_IMPORT and words_string(w[2:]) == 'GLSL.std.450':
            glsl = w[1]
    for w in insts:
        if (w[0] & 0xffff) == OP_TYPE_VECTOR and w[2] == float32 and w[3] == 2:
            vec2 = w[1]

    rewritten, out = 0, []
    for w in insts:
        op = w[0] & 0xffff
        if op == OP_CAPABILITY and CAP_VALVE_FIRST <= w[1] <= CAP_VALVE_LAST:
            if w[1] != CAP_DOT_F16_ACC_F32_VALVE:
                raise ValueError('unsupported VALVE capability %d' % w[1])
            continue
        if op == OP_EXTENSION and words_string(w[1:]) == EXTENSION:
            continue
        if op in (OP_FDOT2_MIX_ACC16_VALVE, OP_FDOT4_MIX_ACC32_VALVE):
            raise ValueError('unsupported VALVE instruction %d' % op)
        out.append(w)

    if float32 is None:
        if any((w[0] & 0xffff) == OP_FDOT2_MIX_ACC32_VALVE for w in insts):
            raise ValueError('OpFDot2MixAcc32VALVE without a 32-bit float type')
        return data, 0

    # Module-level declarations the expansion needs, in their required sections.
    if glsl is None:
        glsl = fresh()
        at = next(k for k, w in enumerate(out) if (w[0] & 0xffff) == OP_MEMORY_MODEL)
        out.insert(at, inst(OP_EXT_INST_IMPORT, glsl, *string_words('GLSL.std.450')))
    if vec2 is None:
        vec2 = fresh()
        at = next(k for k, w in enumerate(out)
                  if (w[0] & 0xffff) == OP_TYPE_FLOAT and w[1] == float32)
        out.insert(at + 1, inst(OP_TYPE_VECTOR, vec2, float32, 2))

    final = []
    for w in out:
        if (w[0] & 0xffff) != OP_FDOT2_MIX_ACC32_VALVE:
            final.append(w)
            continue
        if len(w) != 6:
            raise ValueError('OpFDot2MixAcc32VALVE with %d words' % len(w))
        result_type, result, a, b, acc = w[1:]
        if result_type != float32:
            raise ValueError('OpFDot2MixAcc32VALVE result %%%d is not a 32-bit float' % result)
        a32, b32, ax, ay, bx, by, inner = (fresh() for _ in range(7))
        final += [
            inst(OP_FCONVERT, vec2, a32, a),
            inst(OP_FCONVERT, vec2, b32, b),
            inst(OP_COMPOSITE_EXTRACT, float32, ax, a32, 0),
            inst(OP_COMPOSITE_EXTRACT, float32, ay, a32, 1),
            inst(OP_COMPOSITE_EXTRACT, float32, bx, b32, 0),
            inst(OP_COMPOSITE_EXTRACT, float32, by, b32, 1),
            inst(OP_EXT_INST, float32, inner, glsl, GLSL_FMA, ay, by, acc),
            inst(OP_EXT_INST, float32, result, glsl, GLSL_FMA, ax, bx, inner),
        ]
        rewritten += 1

    header[3] = bound
    flat = header + [x for w in final for x in w]
    return struct.pack('<%dI' % len(flat), *flat), rewritten


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: portable_dot.py <in.spv> <out.spv>')
    with open(sys.argv[1], 'rb') as f:
        data, count = rewrite(f.read())
    with open(sys.argv[2], 'wb') as f:
        f.write(data)
    print('%s: %d mixed float dots rewritten' % (sys.argv[2], count))


if __name__ == '__main__':
    main()
