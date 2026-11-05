// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"

namespace Shader::Backend::SPIRV {

Id EmitUndefU1(EmitContext& ctx) {
    return ctx.OpUndef(ctx.U1[1]);
}

Id EmitUndefU8(EmitContext&) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

Id EmitUndefU16(EmitContext&) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

Id EmitUndefU32(EmitContext& ctx) {
    return ctx.OpUndef(ctx.U32[1]);
}

Id EmitUndefU32x2(EmitContext& ctx) {
    return ctx.OpUndef(ctx.U32[2]);
}

Id EmitUndefU32x3(EmitContext& ctx) {
    return ctx.OpUndef(ctx.U32[3]);
}

Id EmitUndefU32x4(EmitContext& ctx) {
    return ctx.OpUndef(ctx.U32[4]);
}

Id EmitUndefF32(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F32[1]);
}

Id EmitUndefF16(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F16[1]);
}

Id EmitUndefF16x2(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F16[2]);
}

Id EmitUndefF16x3(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F16[3]);
}

Id EmitUndefF16x4(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F16[4]);
}

Id EmitUndefF32x2(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F32[2]);
}

Id EmitUndefF32x3(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F32[3]);
}

Id EmitUndefF32x4(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F32[4]);
}

Id EmitUndefF64(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F64[1]);
}

Id EmitUndefF64x2(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F64[2]);
}

Id EmitUndefF64x3(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F64[3]);
}

Id EmitUndefF64x4(EmitContext& ctx) {
    return ctx.OpUndef(ctx.F64[4]);
}

Id EmitUndefU64(EmitContext&) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

} // namespace Shader::Backend::SPIRV
