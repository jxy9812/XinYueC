#!/usr/bin/env python3
"""生成 XGui Vulkan 渲染驱动的三个 SPIR-V shader（C 数组头文件）。

手写 SPIR-V 1.0 二进制（GLSL450 模型）。顶点布局：pos(2F)+uv(2F)+
color(4F)，location 0/1/2；fragment 输出 location 0。"""
import struct

OP_CAPABILITY = 17
OP_MEMORY_MODEL = 14
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_DECORATE = 71
OP_TYPE_VOID = 19
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_POINTER = 32
OP_TYPE_FUNCTION = 33
OP_CONSTANT = 43
OP_CONSTANT_COMPOSITE = 44
OP_COMPOSITE_CONSTRUCT = 80
OP_COMPOSITE_EXTRACT = 81
OP_VARIABLE = 59
OP_FUNCTION = 54
OP_LABEL = 248
OP_LOAD = 61
OP_STORE = 62
OP_IMAGE_SAMPLE_IMPLICIT_LOD = 87
OP_FMUL = 133
OP_RETURN = 253
OP_FUNCTION_END = 56

CAP_SHADER = 1
STORAGE_INPUT = 1
STORAGE_UNIFORM_CONSTANT = 0
STORAGE_OUTPUT = 3
EXEC_VERTEX = 0
EXEC_FRAGMENT = 4
DEC_BUILTIN = 11
DEC_LOCATION = 30
DEC_DESCRIPTOR_SET = 34
DEC_BINDING = 33
BUILTIN_POSITION = 0


class Builder:
    def __init__(self):
        self.body = []
        self.next_id = 1

    def new_id(self):
        i = self.next_id
        self.next_id += 1
        return i

    def inst(self, opcode, operands=()):
        self.body.append(((len(operands) + 1) << 16) | opcode)
        self.body.extend(operands)

    def literal_string(self, text):
        raw = text.encode() + b"\0"
        while len(raw) % 4:
            raw += b"\0"
        return list(struct.unpack("<%dI" % (len(raw) // 4), raw))

    def f32_bits(self, value):
        return struct.unpack("<I", struct.pack("<f", value))[0]

    def build(self):
        header = [0x07230203, 0x00010000, 0, self.next_id, 0]
        return header + self.body


def gen_vertex():
    b = Builder()
    void_t = b.new_id()
    fn_t = b.new_id()
    f_t = b.new_id()
    v2_t = b.new_id()
    v4_t = b.new_id()
    p_in_v2 = b.new_id()
    p_in_v4 = b.new_id()
    p_out_v2 = b.new_id()
    p_out_v4 = b.new_id()
    f0 = b.new_id()
    f1 = b.new_id()
    in_pos = b.new_id()
    in_uv = b.new_id()
    in_color = b.new_id()
    out_uv = b.new_id()
    out_color = b.new_id()
    gl_pos = b.new_id()
    main = b.new_id()
    entry = b.new_id()
    l_uv = b.new_id()
    l_color = b.new_id()
    l_pos = b.new_id()
    pos_x = b.new_id()
    pos_y = b.new_id()
    clip_pos = b.new_id()

    b.inst(OP_CAPABILITY, [CAP_SHADER])
    b.inst(OP_MEMORY_MODEL, [0, 1])
    b.inst(OP_ENTRY_POINT, [EXEC_VERTEX, main] + b.literal_string("main") +
          [in_pos, in_uv, in_color, out_uv, out_color, gl_pos])
    for loc, vid in ((0, in_pos), (1, in_uv), (2, in_color)):
        b.inst(OP_DECORATE, [vid, DEC_LOCATION, loc])
    b.inst(OP_DECORATE, [out_uv, DEC_LOCATION, 0])
    b.inst(OP_DECORATE, [out_color, DEC_LOCATION, 1])
    b.inst(OP_DECORATE, [gl_pos, DEC_BUILTIN, BUILTIN_POSITION])
    b.inst(OP_TYPE_VOID, [void_t])
    b.inst(OP_TYPE_FLOAT, [f_t, 32])
    b.inst(OP_TYPE_VECTOR, [v2_t, f_t, 2])
    b.inst(OP_TYPE_VECTOR, [v4_t, f_t, 4])
    b.inst(OP_TYPE_POINTER, [p_in_v2, STORAGE_INPUT, v2_t])
    b.inst(OP_TYPE_POINTER, [p_in_v4, STORAGE_INPUT, v4_t])
    b.inst(OP_TYPE_POINTER, [p_out_v2, STORAGE_OUTPUT, v2_t])
    b.inst(OP_TYPE_POINTER, [p_out_v4, STORAGE_OUTPUT, v4_t])
    b.inst(OP_CONSTANT, [f_t, f0, b.f32_bits(0.0)])
    b.inst(OP_CONSTANT, [f_t, f1, b.f32_bits(1.0)])
    b.inst(OP_VARIABLE, [p_in_v2, in_pos, STORAGE_INPUT])
    b.inst(OP_VARIABLE, [p_in_v2, in_uv, STORAGE_INPUT])
    b.inst(OP_VARIABLE, [p_in_v4, in_color, STORAGE_INPUT])
    b.inst(OP_VARIABLE, [p_out_v2, out_uv, STORAGE_OUTPUT])
    b.inst(OP_VARIABLE, [p_out_v4, out_color, STORAGE_OUTPUT])
    b.inst(OP_VARIABLE, [p_out_v4, gl_pos, STORAGE_OUTPUT])
    b.inst(OP_TYPE_FUNCTION, [fn_t, void_t])
    b.inst(OP_FUNCTION, [void_t, main, 0, fn_t])
    b.inst(OP_LABEL, [entry])
    b.inst(OP_LOAD, [v2_t, l_uv, in_uv])
    b.inst(OP_STORE, [out_uv, l_uv])
    b.inst(OP_LOAD, [v4_t, l_color, in_color])
    b.inst(OP_STORE, [out_color, l_color])
    b.inst(OP_LOAD, [v2_t, l_pos, in_pos])
    b.inst(OP_COMPOSITE_EXTRACT, [f_t, pos_x, l_pos, 0])
    b.inst(OP_COMPOSITE_EXTRACT, [f_t, pos_y, l_pos, 1])
    b.inst(OP_COMPOSITE_CONSTRUCT, [v4_t, clip_pos, pos_x, pos_y, f0, f1])
    b.inst(OP_STORE, [gl_pos, clip_pos])
    b.inst(OP_RETURN)
    b.inst(OP_FUNCTION_END)
    return b.build()


def gen_fragment(sampled):
    b = Builder()
    void_t = b.new_id()
    fn_t = b.new_id()
    f_t = b.new_id()
    v2_t = b.new_id()
    v4_t = b.new_id()
    p_in_v2 = b.new_id()
    p_in_v4 = b.new_id()
    p_out_v4 = b.new_id()
    in_uv = b.new_id()
    in_color = b.new_id()
    out_color = b.new_id()
    u_tex = b.new_id()
    main = b.new_id()
    entry = b.new_id()
    l_uv = b.new_id()
    l_color = b.new_id()
    l_samp = b.new_id()
    l_tex = b.new_id()
    l_out = b.new_id()

    img_t = samp_t = p_uni_samp = None
    b.inst(OP_CAPABILITY, [CAP_SHADER])
    b.inst(OP_MEMORY_MODEL, [0, 1])
    entry_ops = [EXEC_FRAGMENT, main] + b.literal_string("main")
    if sampled:
        entry_ops += [in_uv, in_color, out_color]
    else:
        entry_ops += [in_color, out_color]
    b.inst(OP_ENTRY_POINT, entry_ops)
    b.inst(OP_EXECUTION_MODE, [main, 7])  # OriginUpperLeft
    if sampled:
        b.inst(OP_DECORATE, [in_uv, DEC_LOCATION, 0])
    b.inst(OP_DECORATE, [in_color, DEC_LOCATION, 1])
    b.inst(OP_DECORATE, [out_color, DEC_LOCATION, 0])
    if sampled:
        b.inst(OP_DECORATE, [u_tex, DEC_DESCRIPTOR_SET, 0])
        b.inst(OP_DECORATE, [u_tex, DEC_BINDING, 0])
    b.inst(OP_TYPE_VOID, [void_t])
    b.inst(OP_TYPE_FLOAT, [f_t, 32])
    b.inst(OP_TYPE_VECTOR, [v2_t, f_t, 2])
    b.inst(OP_TYPE_VECTOR, [v4_t, f_t, 4])
    b.inst(OP_TYPE_POINTER, [p_in_v2, STORAGE_INPUT, v2_t])
    b.inst(OP_TYPE_POINTER, [p_in_v4, STORAGE_INPUT, v4_t])
    b.inst(OP_TYPE_POINTER, [p_out_v4, STORAGE_OUTPUT, v4_t])
    if sampled:
        img_t = b.new_id()
        samp_t = b.new_id()
        p_uni_samp = b.new_id()
        b.inst(OP_TYPE_IMAGE, [img_t, f_t, 1, 0, 0, 0, 1, 0])
        b.inst(OP_TYPE_SAMPLED_IMAGE, [samp_t, img_t])
        b.inst(OP_TYPE_POINTER, [p_uni_samp, STORAGE_UNIFORM_CONSTANT, samp_t])
    b.inst(OP_VARIABLE, [p_in_v2, in_uv, STORAGE_INPUT])
    b.inst(OP_VARIABLE, [p_in_v4, in_color, STORAGE_INPUT])
    b.inst(OP_VARIABLE, [p_out_v4, out_color, STORAGE_OUTPUT])
    if sampled:
        b.inst(OP_VARIABLE, [p_uni_samp, u_tex, STORAGE_UNIFORM_CONSTANT])
    b.inst(OP_TYPE_FUNCTION, [fn_t, void_t])
    b.inst(OP_FUNCTION, [void_t, main, 0, fn_t])
    b.inst(OP_LABEL, [entry])
    if sampled:
        b.inst(OP_LOAD, [v2_t, l_uv, in_uv])
    b.inst(OP_LOAD, [v4_t, l_color, in_color])
    if sampled:
        b.inst(OP_LOAD, [samp_t, l_samp, u_tex])
        b.inst(OP_IMAGE_SAMPLE_IMPLICIT_LOD, [v4_t, l_tex, l_samp, l_uv])
        b.inst(OP_FMUL, [v4_t, l_out, l_tex, l_color])
        b.inst(OP_STORE, [out_color, l_out])
    else:
        b.inst(OP_STORE, [out_color, l_color])
    b.inst(OP_RETURN)
    b.inst(OP_FUNCTION_END)
    return b.build()


def c_array(name, words):
    lines = [f"static const uint32_t {name}[] = {{"]
    for i in range(0, len(words), 6):
        chunk = ", ".join("0x%08xu" % w for w in words[i:i + 6])
        lines.append("    " + chunk + ",")
    lines.append("};")
    lines.append(f"#define {name.upper()}_WORDS {len(words)}u")
    return "\n".join(lines)


out = ["""/******************************************************************************
 * @file       XGpuRenderDriver_vulkan_shaders.h
 * @brief      Vulkan 渲染驱动的 SPIR-V shader（构建期生成，手工核对的
 *             最小 SPIR-V 1.0 二进制）。
 * @details    三个 shader 共用顶点布局：location 0=pos(vec2)、1=uv(vec2)、
 *             2=color(vec4，预乘)。顶点着色器将位置属性扩展为
 *             gl_Position=(pos.x,pos.y,0,1)（位置已在 NDC 空间）；片段输出
 *             location 0。纹理片段做 texture(uv)*color。由
 *             tools/gen_spv.py 生成，勿手改。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGPURENDERDRIVER_VULKAN_SHADERS_H
#define XGPURENDERDRIVER_VULKAN_SHADERS_H

#include <stdint.h>
"""]
out.append(c_array("kSpvVertex", gen_vertex()))
out.append(c_array("kSpvFragmentSolid", gen_fragment(False)))
out.append(c_array("kSpvFragmentTexture", gen_fragment(True)))
out.append("\n#endif /* XGPURENDERDRIVER_VULKAN_SHADERS_H */\n")
open(
    "Src/XGui/Graphics/XGpuRenderDriver_vulkan_shaders.h",
    "w",
    encoding="utf-8",
    newline="\n",
).write("\n".join(out))
print("shaders header generated")
