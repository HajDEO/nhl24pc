// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string>
#include "common/assert.h"
#include "common/logging/log.h"
#include "shader_recompiler/frontend/control_flow_graph.h"
#include "shader_recompiler/ir/condition.h"

namespace Shader::Gcn {

struct Compare {
    bool operator()(const Block& lhs, u32 rhs) const noexcept {
        return lhs.begin < rhs;
    }

    bool operator()(u32 lhs, const Block& rhs) const noexcept {
        return lhs < rhs.begin;
    }

    bool operator()(const Block& lhs, const Block& rhs) const noexcept {
        return lhs.begin < rhs.begin;
    }
};

static IR::Condition MakeCondition(const GcnInst& inst) {
    if (inst.IsCmpx()) {
        return IR::Condition::Execnz;
    }

    switch (inst.opcode) {
    case Opcode::S_CBRANCH_SCC0:
        return IR::Condition::Scc0;
    case Opcode::S_CBRANCH_SCC1:
        return IR::Condition::Scc1;
    case Opcode::S_CBRANCH_VCCZ:
        return IR::Condition::Vccz;
    case Opcode::S_CBRANCH_VCCNZ:
        return IR::Condition::Vccnz;
    case Opcode::S_CBRANCH_EXECZ:
        return IR::Condition::Execz;
    case Opcode::S_CBRANCH_EXECNZ:
        return IR::Condition::Execnz;
    default:
        return IR::Condition::True;
    }
}

static bool IgnoresExecMask(const GcnInst& inst) {
    // EXEC mask does not affect scalar instructions or branches.
    switch (inst.category) {
    case InstCategory::ScalarALU:
    case InstCategory::ScalarMemory:
    case InstCategory::FlowControl:
        return true;
    default:
        break;
    }
    // Read/Write Lane instructions are not affected either.
    switch (inst.opcode) {
    case Opcode::V_READLANE_B32:
    case Opcode::V_WRITELANE_B32:
    case Opcode::V_READFIRSTLANE_B32:
        return true;
    default:
        break;
    }
    return false;
}

static std::optional<u32> ResolveSetPcTarget(std::span<const GcnInst> list, u32 setpc_index,
                                             std::span<const u32> pc_map) {
    if (setpc_index < 3) {
        return std::nullopt;
    }

    const auto& getpc = list[setpc_index - 3];
    const auto& arith = list[setpc_index - 2];
    const auto& setpc = list[setpc_index];

    if (getpc.opcode != Opcode::S_GETPC_B64 ||
        !(arith.opcode == Opcode::S_ADD_U32 || arith.opcode == Opcode::S_SUB_U32) ||
        setpc.opcode != Opcode::S_SETPC_B64)
        return std::nullopt;

    if (getpc.dst[0].code != setpc.src[0].code || arith.dst[0].code != setpc.src[0].code)
        return std::nullopt;

    if (arith.src_count < 2 || arith.src[1].field != OperandField::LiteralConst)
        return std::nullopt;

    const u32 imm = arith.src[1].code;

    const s32 signed_offset =
        (arith.opcode == Opcode::S_ADD_U32) ? static_cast<s32>(imm) : -static_cast<s32>(imm);

    const u32 base_pc = pc_map[setpc_index - 3] + getpc.length;

    const u32 result_pc = static_cast<u32>(static_cast<s32>(base_pc) + signed_offset);
    LOG_DEBUG(Render_Recompiler, "SetPC target: {} + {} = {}", base_pc, signed_offset, result_pc);
    return result_pc & ~0x3u;
}

namespace {

struct Replacement {
    u32 begin; // byte range in the original code that is replaced
    u32 end;
    std::vector<u32> words;
};

struct TableEntry {
    u32 offset;    // byte offset of the entry from the instruction after s_getpc_b64
    u32 mov_dword; // dword index of the v_mov_b32 in the original code
    u32 mov_count; // size of the v_mov_b32 in dwords
};

// Rewrites one register-indexed jump table, whose s_setpc_b64 is insts[i]:
//   s_getpc_b64  sA:sA+1
//   s_add_u32    sA, sA, sX        (sX = byte offset from the next instruction)
//   s_addc_u32   sA+1, sA+1, ...
//   s_setpc_b64  sA:sA+1
// followed by [s_waitcnt] v_mov_b32 vN, <src> [s_branch exit] entries. Shader compilers emit this
// to read a VGPR selected by a runtime index. It is a dynamic jump the recompiler cannot resolve,
// so it becomes a chain of "if (index == k) vN = src" checks.
bool AnalyzeJumpTable(std::span<const u32> code, std::span<const GcnInst> insts,
                      const std::vector<u32>& pcs, u32 i, std::vector<Replacement>& repl) {
    const u32 count = static_cast<u32>(insts.size());
    const u32 total = pcs[count];
    if (i < 3) {
        return false;
    }
    const GcnInst& getpc = insts[i - 3];
    const GcnInst& add = insts[i - 2];
    const GcnInst& addc = insts[i - 1];
    const GcnInst& setpc = insts[i];
    if (getpc.opcode != Opcode::S_GETPC_B64 || add.opcode != Opcode::S_ADD_U32 ||
        addc.opcode != Opcode::S_ADDC_U32 || add.src_count < 2) {
        return false;
    }
    const u32 reg = getpc.dst[0].code;
    if (setpc.src[0].code != reg || add.dst[0].code != reg || addc.dst[0].code != reg + 1) {
        return false;
    }
    const InstOperand* offset_op = nullptr;
    if (add.src[0].field == OperandField::ScalarGPR && add.src[0].code == reg) {
        offset_op = &add.src[1];
    } else if (add.src[1].field == OperandField::ScalarGPR && add.src[1].code == reg) {
        offset_op = &add.src[0];
    }
    if (!offset_op || offset_op->field != OperandField::ScalarGPR || offset_op->code > 103) {
        return false;
    }
    const u32 offset_reg = offset_op->code;

    // Parse the table that follows the jump.
    const u32 base_pc = pcs[i - 3] + getpc.length;
    std::vector<TableEntry> entries;
    u32 exit_pc = 0;
    u32 j = i + 1;
    while (j < count) {
        const u32 entry_pc = pcs[j];
        u32 k = j;
        while (k < count && insts[k].opcode == Opcode::S_WAITCNT) {
            k++;
        }
        if (k >= count || insts[k].opcode != Opcode::V_MOV_B32 || insts[k].length % 4 != 0) {
            return false;
        }
        entries.push_back({entry_pc - base_pc, pcs[k] / 4, insts[k].length / 4});
        k++;
        if (k < count && insts[k].opcode == Opcode::S_BRANCH) {
            const u32 target = insts[k].BranchTarget(pcs[k]);
            if (exit_pc != 0 && target != exit_pc) {
                return false;
            }
            exit_pc = target;
            j = k + 1;
            continue;
        }
        // No branch: last entry falls through to the exit.
        if (exit_pc != 0 && pcs[k] != exit_pc) {
            return false;
        }
        exit_pc = pcs[k];
        break;
    }
    if (entries.size() < 2 || exit_pc <= pcs[i] || exit_pc > total) {
        return false;
    }
    if (!std::ranges::binary_search(pcs, exit_pc)) {
        return false;
    }
    const u32 region_begin = pcs[i - 3];
    const u32 region_end = exit_pc;

    // Many compilers load the jump offset from a table stored in the shader binary:
    //   s_getpc_b64 sP; s_add_u32 sP, sP, LIT; s_addc_u32 sP+1, sP+1, 0;
    //   s_load_dwordx2 sX:sX+1, sP:sP+1, sIDX        (sIDX = 8 * index)
    // Read that table so the checks can compare the index itself instead.
    u32 compare_reg = offset_reg;
    std::vector<std::pair<u32, u32>> selectors; // (value compared with compare_reg, entry index)
    std::vector<u32> killed_regs = {reg, reg + 1};
    Replacement load_removal{0, 0, {}};
    bool has_load_removal = false;
    u32 ld = 0;
    for (u32 n = i - 4; n >= 3 && n + 80 > i; n--) {
        // look back a short distance for the load that defines the offset register
        if (insts[n].opcode == Opcode::S_LOAD_DWORDX2 && insts[n].dst[0].code == offset_reg) {
            ld = n;
            break;
        }
    }
    if (ld != 0 && insts[ld].control.smrd.imm == 0 &&
        insts[ld].src[1].field == OperandField::ScalarGPR) {
        const u32 sbase = insts[ld].src[0].code * 2;
        const u32 idx_reg = insts[ld].src[1].code;
        const GcnInst& g = insts[ld - 3];
        const GcnInst& a1 = insts[ld - 2];
        const GcnInst& a2 = insts[ld - 1];
        const bool shape_ok =
            g.opcode == Opcode::S_GETPC_B64 && g.dst[0].code == sbase &&
            a1.opcode == Opcode::S_ADD_U32 && a1.dst[0].code == sbase &&
            a2.opcode == Opcode::S_ADDC_U32 && a2.dst[0].code == sbase + 1 &&
            ((a1.src[0].field == OperandField::ScalarGPR && a1.src[0].code == sbase &&
              a1.src[1].field == OperandField::LiteralConst) ||
             (a1.src[1].field == OperandField::ScalarGPR && a1.src[1].code == sbase &&
              a1.src[0].field == OperandField::LiteralConst));
        if (!shape_ok) {
            return false;
        }
        const u32 table_literal = a1.src[0].field == OperandField::LiteralConst ? a1.src[0].code
                                                                                 : a1.src[1].code;
        // Nothing between the load and the jump may use the registers being removed.
        for (u32 n = ld + 1; n < i - 3; n++) {
            for (u32 sidx = 0; sidx < insts[n].src_count && sidx < 4; sidx++) {
                if (insts[n].src[sidx].field != OperandField::ScalarGPR) {
                    continue;
                }
                u32 c = insts[n].src[sidx].code;
                if (insts[n].encoding == InstEncoding::SMRD && sidx == 0) {
                    c *= 2;
                }
                if (c == offset_reg || c == offset_reg + 1 || c == sbase || c == sbase + 1) {
                    return false;
                }
            }
        }
        // The index register must still hold the same value at the jump.
        for (u32 n = ld + 1; n < i - 3; n++) {
            for (u32 d = 0; d < insts[n].dst_count; d++) {
                if (insts[n].dst[d].field == OperandField::ScalarGPR &&
                    (insts[n].dst[d].code == idx_reg || insts[n].dst[d].code + 1 == idx_reg)) {
                    return false;
                }
            }
        }
        // The index may be biased (s_add_u32 sIDX, sIDX, imm) to select a table that starts
        // further into the same data array.
        u32 bias = 0;
        for (u32 n = ld - 1; n > ld - 12 && n >= 3; n--) {
            bool writes_idx = false;
            for (u32 d = 0; d < 2; d++) {
                if (insts[n].dst[d].field == OperandField::ScalarGPR &&
                    insts[n].dst[d].code == idx_reg) {
                    writes_idx = true;
                }
            }
            if (!writes_idx) {
                continue;
            }
            if (insts[n].opcode == Opcode::S_ADD_U32) {
                const InstOperand& lhs = insts[n].src[0];
                const InstOperand& rhs = insts[n].src[1];
                const InstOperand* imm = nullptr;
                if (lhs.field == OperandField::ScalarGPR && lhs.code == idx_reg) {
                    imm = &rhs;
                } else if (rhs.field == OperandField::ScalarGPR && rhs.code == idx_reg) {
                    imm = &lhs;
                }
                if (!imm) {
                    return false;
                }
                if (imm->field == OperandField::LiteralConst) {
                    bias = imm->code;
                } else if (imm->field == OperandField::SignedConstIntPos) {
                    bias = imm->code - 128;
                } else if (imm->field == OperandField::ConstZero) {
                    bias = 0;
                } else {
                    return false;
                }
            }
            break;
        }
        const u32 table_pc = pcs[ld - 3] + g.length + table_literal + bias;
        const u32* base_ptr = code.data();
        std::vector<u32> valid_offsets;
        for (const auto& e : entries) {
            valid_offsets.push_back(e.offset);
        }
        valid_offsets.push_back(exit_pc - base_pc);
        std::string dump;
        for (u32 k = 0; k < 64; k++) {
            const u32 lo = base_ptr[table_pc / 4 + k * 2];
            const u32 hi = base_ptr[table_pc / 4 + k * 2 + 1];
            if (k < 20) {
                dump += fmt::format(" {:x}:{:x}", hi, lo);
            }
            if (hi != 0 || std::ranges::find(valid_offsets, lo) == valid_offsets.end()) {
                break;
            }
            if (lo == exit_pc - base_pc) {
                continue; // jumps straight to the end: nothing to do
            }
            const auto it =
                std::ranges::find_if(entries, [&](const TableEntry& e) { return e.offset == lo; });
            selectors.emplace_back(bias + k * 8, static_cast<u32>(it - entries.begin()));
        }
        LOG_DEBUG(Render_Recompiler, "Jump table data at {:#x}:{}", table_pc, dump);
        if (selectors.empty()) {
            return false;
        }
        compare_reg = idx_reg;
        load_removal = {pcs[ld - 3], pcs[ld] + insts[ld].length, {}};
        has_load_removal = true;
        for (u32 r : {sbase, sbase + 1, offset_reg, offset_reg + 1}) {
            killed_regs.push_back(r);
        }
    } else {
        for (u32 k = 0; k < entries.size(); k++) {
            selectors.emplace_back(entries[k].offset, k);
        }
    }
    if (compare_reg > 103) {
        return false;
    }

    // Registers whose definition is going away must not be read afterwards.
    for (u32 r : killed_regs) {
        for (u32 n = 0; n < count; n++) {
            if (pcs[n] < region_end) {
                continue;
            }
            const GcnInst& inst = insts[n];
            bool reads = false;
            for (u32 sidx = 0; sidx < inst.src_count && sidx < 4; sidx++) {
                if (inst.src[sidx].field != OperandField::ScalarGPR) {
                    continue;
                }
                u32 c = inst.src[sidx].code;
                if (inst.encoding == InstEncoding::SMRD && sidx == 0) {
                    c *= 2;
                    if (r == c || r == c + 1) {
                        reads = true;
                    }
                } else if (c == r ||
                           (inst.src[sidx].type == ScalarType::Uint64 && c + 1 == r)) {
                    reads = true;
                }
            }
            if (reads) {
                return false;
            }
            bool writes = false;
            for (u32 d = 0; d < 2; d++) {
                if (inst.dst[d].field != OperandField::ScalarGPR) {
                    continue;
                }
                // Scalar memory loads and 64-bit results cover several consecutive registers.
                u32 width = inst.dst[d].type == ScalarType::Uint64 ? 2 : 1;
                if (inst.encoding == InstEncoding::SMRD) {
                    width = std::max<u32>(1, inst.control.smrd.count);
                }
                if (r >= inst.dst[d].code && r < inst.dst[d].code + width) {
                    writes = true;
                }
            }
            if (writes) {
                break;
            }
        }
    }

    // Build the replacement.
    std::vector<u32> region;
    for (const auto& [value, entry_index] : selectors) {
        const TableEntry& e = entries[entry_index];
        constexpr u32 SOPC = 0xBF000000;
        constexpr u32 SOPP = 0xBF800000;
        if (value <= 64) {
            region.push_back(SOPC | (6u << 16) | ((128 + value) << 8) | compare_reg);
        } else {
            region.push_back(SOPC | (6u << 16) | (255u << 8) | compare_reg);
            region.push_back(value);
        }
        // Skip the v_mov when the index does not match.
        region.push_back(SOPP | (4u << 16) | e.mov_count);
        for (u32 d = 0; d < e.mov_count; d++) {
            region.push_back(code[e.mov_dword + d]);
        }
    }
    if (has_load_removal) {
        repl.push_back(std::move(load_removal));
    }
    repl.push_back({region_begin, region_end, std::move(region)});
    return true;
}

} // namespace

bool ExpandIndirectRegisterTables(std::span<const u32> code, std::span<const GcnInst> insts,
                                  std::vector<u32>& out) {
    const u32 count = static_cast<u32>(insts.size());
    std::vector<u32> pcs(count + 1);
    u32 total = 0;
    for (u32 i = 0; i < count; i++) {
        pcs[i] = total;
        total += insts[i].length;
    }
    pcs[count] = total;
    if (total != code.size() * sizeof(u32)) {
        return false;
    }

    std::vector<Replacement> repl;
    u32 tables = 0;
    for (u32 i = 3; i < count; i++) {
        if (insts[i].opcode == Opcode::S_SETPC_B64 && !ResolveSetPcTarget(insts, i, pcs)) {
            if (!AnalyzeJumpTable(code, insts, pcs, i, repl)) {
                return false;
            }
            tables++;
        }
    }
    if (tables == 0) {
        return false;
    }
    std::ranges::sort(repl, {}, &Replacement::begin);
    for (size_t n = 1; n < repl.size(); n++) {
        if (repl[n - 1].end > repl[n].begin) {
            return false;
        }
    }

    bool bad_target = false;
    const auto map_pc = [&](u32 pc) -> u32 {
        s64 shift = 0;
        for (const auto& r : repl) {
            if (pc >= r.end) {
                shift += static_cast<s64>(r.words.size() * sizeof(u32)) -
                         static_cast<s64>(r.end - r.begin);
            } else if (pc > r.begin && pc < r.end) {
                bad_target = true;
            }
        }
        return static_cast<u32>(static_cast<s64>(pc) + shift);
    };
    const auto replaced = [&](u32 pc) {
        return std::ranges::any_of(
            repl, [&](const Replacement& r) { return pc >= r.begin && pc < r.end; });
    };
    const u32 first_begin = repl.front().begin;

    // Relative branches around the rewritten ranges have to be re-encoded with the new distances.
    std::vector<std::pair<u32, u16>> fixups; // (new dword index, new simm)
    for (u32 n = 0; n < count; n++) {
        const u32 pc = pcs[n];
        if (replaced(pc)) {
            continue;
        }
        const GcnInst& inst = insts[n];
        if (inst.IsFork() || inst.opcode == Opcode::S_SWAPPC_B64) {
            // Not safe to move.
            if (pc >= first_begin) {
                return false;
            }
            continue;
        }
        if (!inst.IsUnconditionalBranch() && !inst.IsConditionalBranch()) {
            continue;
        }
        const u32 target = inst.BranchTarget(pc);
        const s64 simm = (static_cast<s64>(map_pc(target)) - static_cast<s64>(map_pc(pc)) - 4) / 4;
        if (simm < INT16_MIN || simm > INT16_MAX) {
            return false;
        }
        fixups.emplace_back(map_pc(pc) / 4, static_cast<u16>(static_cast<s16>(simm)));
    }

    // Constant-offset jumps (s_getpc_b64 / s_add_u32 literal / s_setpc_b64) that cross a rewritten
    // range need their literal recomputed too.
    std::vector<std::pair<u32, u32>> literal_fixups; // (new dword index, new literal)
    for (u32 n = 3; n < count; n++) {
        const u32 pc = pcs[n];
        if (insts[n].opcode != Opcode::S_SETPC_B64 || replaced(pc)) {
            continue;
        }
        const auto target = ResolveSetPcTarget(insts, n, pcs);
        if (!target) {
            return false;
        }
        const GcnInst& arith = insts[n - 2];
        const u32 base_pc = pcs[n - 3] + insts[n - 3].length;
        const s32 new_offset =
            static_cast<s32>(map_pc(*target)) - static_cast<s32>(map_pc(base_pc));
        const u32 literal = arith.opcode == Opcode::S_ADD_U32 ? static_cast<u32>(new_offset)
                                                              : static_cast<u32>(-new_offset);
        literal_fixups.emplace_back(map_pc(pcs[n - 2]) / 4 + 1, literal);
    }
    if (bad_target) {
        return false; // something jumps into the middle of a rewritten range
    }

    out.clear();
    out.reserve(code.size());
    u32 cursor = 0;
    for (const auto& r : repl) {
        out.insert(out.end(), code.begin() + cursor / 4, code.begin() + r.begin / 4);
        out.insert(out.end(), r.words.begin(), r.words.end());
        cursor = r.end;
    }
    out.insert(out.end(), code.begin() + cursor / 4, code.end());
    for (const auto& [index, simm] : fixups) {
        out[index] = (out[index] & 0xFFFF0000u) | simm;
    }
    for (const auto& [index, literal] : literal_fixups) {
        out[index] = literal;
    }
    LOG_INFO(Render_Recompiler, "Expanded {} register-indexed jump table(s)", tables);
    return true;
}

static constexpr size_t LabelReserveSize = 32;

CFG::CFG(Common::ObjectPool<Block>& block_pool_, std::span<const GcnInst> inst_list_)
    : block_pool{block_pool_}, inst_list{inst_list_} {
    index_to_pc.resize(inst_list.size() + 1);
    labels.reserve(LabelReserveSize);
    EmitLabels();
    EmitBlocks();
    LinkBlocks();
    SplitDivergenceScopes();
    RemoveUnreachableBlocks();
}

void CFG::EmitLabels() {
    // Always set a label at entry point.
    u32 pc = 0;
    AddLabel(pc);

    // Iterate instruction list and add labels to branch targets.
    for (u32 i = 0; i < inst_list.size(); i++) {
        index_to_pc[i] = pc;
        const GcnInst inst = inst_list[i];
        if (inst.IsUnconditionalBranch()) {
            u32 target = inst.BranchTarget(pc);
            if (inst.opcode == Opcode::S_SETPC_B64) {
                if (auto t = ResolveSetPcTarget(inst_list, i, index_to_pc)) {
                    target = *t;
                } else {
                    ASSERT_MSG(
                        false,
                        "S_SETPC_B64 without a resolvable offset at PC {:#x} (Index {}): Involved "
                        "instructions not recognized or invalid pattern",
                        pc, i);
                }
            }
            AddLabel(target);
            // Emit this label so that the block ends with the branching instruction
            AddLabel(pc + inst.length);
        } else if (inst.IsConditionalBranch()) {
            const u32 true_label = inst.BranchTarget(pc);
            const u32 false_label = pc + inst.length;
            if (true_label != false_label) {
                AddLabel(true_label);
                AddLabel(false_label);
            }
        } else if (inst.opcode == Opcode::S_ENDPGM) {
            const u32 next_label = pc + inst.length;
            AddLabel(next_label);
        }

        pc += inst.length;
    }
    index_to_pc[inst_list.size()] = pc;

    // Sort labels to make sure block insertion is correct.
    std::ranges::sort(labels);
}

void CFG::SplitDivergenceScopes() {
    const auto is_open_scope = [](const GcnInst& inst) {
        // An open scope instruction is an instruction that modifies EXEC
        // but also saves the previous value to restore later. This indicates
        // we are entering a scope.
        return inst.opcode == Opcode::S_AND_SAVEEXEC_B64 ||
               // While this instruction does not save EXEC it is often used paired
               // with SAVEEXEC to mask the threads that didn't pass the condition
               // of initial branch.
               (inst.opcode == Opcode::S_ANDN2_B64 && inst.dst[0].field == OperandField::ExecLo) ||
               inst.IsCmpx();
    };
    const auto is_close_scope = [](const GcnInst& inst) {
        // Closing an EXEC scope can be either a branch instruction
        // (typical case when S_AND_SAVEEXEC_B64 is right before a branch)
        // or by a move instruction to EXEC that restores the backup.
        return (inst.opcode == Opcode::S_MOV_B64 && inst.dst[0].field == OperandField::ExecLo) ||
               // Sometimes compiler might insert instructions between the SAVEEXEC and the branch.
               // Those instructions need to be wrapped in the condition as well so allow branch
               // as end scope instruction.
               inst.opcode == Opcode::S_CBRANCH_EXECZ || inst.opcode == Opcode::S_ENDPGM ||
               (inst.opcode == Opcode::S_ANDN2_B64 && inst.dst[0].field == OperandField::ExecLo);
    };

    for (auto blk = blocks.begin(); blk != blocks.end(); blk++) {
        auto next_blk = std::next(blk);
        s32 curr_begin = -1;
        for (size_t index = blk->begin_index; index <= blk->end_index; index++) {
            const auto& inst = inst_list[index];
            const bool is_close = is_close_scope(inst);
            if ((is_close || index == blk->end_index) && curr_begin != -1) {
                // If there are no instructions inside scope don't do anything.
                if (index - curr_begin == 1 && is_close) {
                    curr_begin = is_open_scope(inst) ? static_cast<s32>(index) : -1;
                    continue;
                }
                // If all instructions in the scope ignore exec masking, we shouldn't insert a
                // scope.
                const auto start = inst_list.begin() + curr_begin + 1;
                if (!std::ranges::all_of(start, inst_list.begin() + index + !is_close,
                                         IgnoresExecMask)) {
                    // Determine the first instruction affected by the exec mask.
                    do {
                        ++curr_begin;
                    } while (IgnoresExecMask(inst_list[curr_begin]));

                    // Determine the last instruction affected by the exec mask.
                    s32 curr_end = index;
                    while (IgnoresExecMask(inst_list[curr_end])) {
                        --curr_end;
                    }

                    // Create a new block for the divergence scope.
                    Block* block = block_pool.Create();
                    block->begin = index_to_pc[curr_begin];
                    block->end = index_to_pc[curr_end];
                    block->begin_index = curr_begin;
                    block->end_index = curr_end;
                    block->end_inst = inst_list[curr_end];
                    block->num_predecessors = 1;
                    blocks.insert_before(next_blk, *block);

                    // If we are inside the parent block, make an epilogue block and jump to it.
                    if (curr_end != blk->end_index) {
                        Block* epi_block = block_pool.Create();
                        epi_block->begin = index_to_pc[curr_end + 1];
                        epi_block->end = blk->end;
                        epi_block->begin_index = curr_end + 1;
                        epi_block->end_index = blk->end_index;
                        epi_block->end_inst = blk->end_inst;
                        epi_block->cond = blk->cond;
                        epi_block->end_class = blk->end_class;
                        epi_block->branch_true = blk->branch_true;
                        epi_block->branch_false = blk->branch_false;
                        epi_block->num_predecessors = 2;
                        blocks.insert_before(next_blk, *epi_block);

                        // Have divergence block always jump to epilogue block.
                        block->cond = IR::Condition::True;
                        block->branch_true = epi_block;
                        block->branch_false = nullptr;

                        // If the parent block fails to enter divergence block make it jump to
                        // epilogue too
                        blk->branch_false = epi_block;
                    } else {
                        // No epilogue block is needed since the divergence block
                        // also ends the parent block. Inherit the end condition.
                        auto& parent_blk = *blk;
                        ASSERT(blk->cond == IR::Condition::True && blk->branch_true);
                        block->cond = IR::Condition::True;
                        block->branch_true = blk->branch_true;
                        block->branch_false = nullptr;

                        // If the parent block didn't enter the divergence scope
                        // have it jump directly to the next one
                        blk->branch_false = blk->branch_true;
                        blk->branch_true->num_predecessors++;
                    }

                    // Shrink parent block to end right before curr_begin
                    // and make it jump to divergence block
                    --curr_begin;
                    blk->end = index_to_pc[curr_begin];
                    blk->end_index = curr_begin;
                    blk->end_inst = inst_list[curr_begin];
                    blk->cond = IR::Condition::Execnz;
                    blk->end_class = EndClass::Branch;
                    blk->branch_true = block;
                }
                // Reset scope begin.
                curr_begin = -1;
            }
            // Mark a potential start of an exec scope.
            if (is_open_scope(inst)) {
                curr_begin = index;
            }
        }
    }
}

void CFG::EmitBlocks() {
    for (auto it = labels.cbegin(); it != labels.cend(); ++it) {
        const Label start = *it;
        const auto next_it = std::next(it);
        const bool is_last = (next_it == labels.cend());
        if (is_last) {
            // Last label is special.
            return;
        }
        // The end label is the start instruction of next block.
        // The end instruction of this block is the previous one.
        const Label end = *next_it;
        const size_t end_index = GetIndex(end) - 1;
        const auto& end_inst = inst_list[end_index];

        // Insert block between the labels using the last instruction
        // as an indicator for branching type.
        Block* block = block_pool.Create();
        block->begin = start;
        block->end = end;
        block->begin_index = GetIndex(start);
        block->end_index = end_index;
        block->end_inst = end_inst;
        block->cond = MakeCondition(end_inst);
        blocks.insert(*block);
    }
}

void CFG::LinkBlocks() {
    const auto get_block = [this](u32 address) {
        auto it = blocks.find(address, Compare{});
        ASSERT_MSG(it != blocks.cend() && it->begin == address);
        return &*it;
    };

    for (auto it = blocks.begin(); it != blocks.end(); it++) {
        auto& block = *it;
        const auto end_inst{block.end_inst};

        // If the block doesn't end with a branch we simply
        // need to link with the next block.
        if (!end_inst.IsTerminateInstruction()) {
            auto* next_block = get_block(block.end);
            block.branch_true = next_block;
            block.end_class = EndClass::Branch;
            next_block->num_predecessors++;
            continue;
        }

        // Find the branch targets from the instruction and link the blocks.
        // Note: Block end address is one instruction after end_inst.
        const u32 branch_pc = block.end - end_inst.length;
        u32 target_pc = 0;
        if (end_inst.opcode == Opcode::S_SETPC_B64) {
            auto tgt = ResolveSetPcTarget(inst_list, block.end_index, index_to_pc);
            ASSERT_MSG(tgt,
                       "S_SETPC_B64 without a resolvable offset at PC {:#x} (Index {}): Involved "
                       "instructions not recognized or invalid pattern",
                       branch_pc, block.end_index);
            target_pc = *tgt;
        } else {
            target_pc = end_inst.BranchTarget(branch_pc);
        }

        if (end_inst.IsUnconditionalBranch()) {
            auto* target_block = get_block(target_pc);
            block.branch_true = target_block;
            block.end_class = EndClass::Branch;
            target_block->num_predecessors++;
        } else if (end_inst.IsConditionalBranch()) {
            auto* end_block = get_block(block.end);
            block.end_class = EndClass::Branch;
            if (target_pc != block.end) {
                auto* target_block = get_block(target_pc);
                block.branch_true = target_block;
                block.branch_false = end_block;
                target_block->num_predecessors++;
            } else {
                block.branch_true = end_block;
                block.cond = IR::Condition::True;
            }
            end_block->num_predecessors++;
        } else if (end_inst.opcode == Opcode::S_ENDPGM) {
            block.end_class = EndClass::Exit;
        } else {
            UNREACHABLE();
        }
    }
}

void CFG::RemoveUnreachableBlocks() {
    for (auto it = std::next(blocks.begin()); it != blocks.end();) {
        if (it->num_predecessors == 0) {
            LOG_WARNING(Render_Recompiler, "Removing unreachable block begin={:#x}", it->begin);
            it = blocks.erase(it);
        } else {
            it++;
        }
    }
}

std::string CFG::Dot() const {
    int node_uid{0};

    const auto name_of = [](const Block& block) { return fmt::format("\"{:#x}\"", block.begin); };

    std::string dot{"digraph shader {\n"};
    dot += fmt::format("\tsubgraph cluster_{} {{\n", 0);
    dot += fmt::format("\t\tnode [style=filled];\n");
    for (const Block& block : blocks) {
        const std::string name{name_of(block)};
        const auto add_branch = [&](Block* branch, bool add_label) {
            dot += fmt::format("\t\t{}->{}", name, name_of(*branch));
            if (add_label && block.cond != IR::Condition::True &&
                block.cond != IR::Condition::False) {
                dot += fmt::format(" [label=\"{}\"]", block.cond);
            }
            dot += '\n';
        };
        dot += fmt::format("\t\t{};\n", name);
        switch (block.end_class) {
        case EndClass::Branch:
            if (block.cond != IR::Condition::False) {
                add_branch(block.branch_true, true);
            }
            if (block.cond != IR::Condition::True) {
                add_branch(block.branch_false, false);
            }
            break;
        case EndClass::Exit:
            dot += fmt::format("\t\t{}->N{};\n", name, node_uid);
            dot +=
                fmt::format("\t\tN{} [label=\"Exit\"][shape=square][style=stripped];\n", node_uid);
            ++node_uid;
            break;
        }
    }
    dot += "\t\tlabel = \"main\";\n\t}\n";
    if (blocks.empty()) {
        dot += "Start;\n";
    } else {
        dot += fmt::format("\tStart -> {};\n", name_of(*blocks.begin()));
    }
    dot += fmt::format("\tStart [shape=diamond];\n");
    dot += "}\n";
    return dot;
}

} // namespace Shader::Gcn
