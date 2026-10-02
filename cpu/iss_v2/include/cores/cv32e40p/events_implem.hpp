// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include "cpu/iss_v2/include/cores/cv32e40p/csr.hpp"
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/fdsu.hpp>

inline void Cv32e40pEvents::event_load_account(int incr)
{
    Events::event_load_account(incr);
    this->pending_events |= CV32E40P_HPM_LD;
}

inline void Cv32e40pEvents::event_store_account(int incr)
{
    Events::event_store_account(incr);
    this->pending_events |= CV32E40P_HPM_ST;
}

inline void Cv32e40pEvents::event_branch_account()
{
    Events::event_branch_account();
    this->pending_events |= CV32E40P_HPM_BRANCH;
}

inline void Cv32e40pEvents::event_taken_branch_account()
{
    // A taken branch fires both RTL event lines.
    Events::event_taken_branch_account();
    this->pending_events |= CV32E40P_HPM_BRANCH | CV32E40P_HPM_BRANCH_TAKEN;
    // The branch is decided in EX, so the two instructions fetched behind it are flushed.
    this->iss.exec.stall_cycles_inc(2);
    this->redirect = true;
}

inline void Cv32e40pEvents::event_jump_account()
{
    Events::event_jump_account();
    this->pending_events |= CV32E40P_HPM_JUMP;
    this->jump_fetch_account();
}

inline void Cv32e40pEvents::event_jalr_account(int rs1)
{
    // The RTL jump line fires on JALR, c.jr and c.jalr too (cv32e40p_id_stage.sv).
    Events::event_jalr_account(rs1);
    this->pending_events |= CV32E40P_HPM_JUMP;
    this->jump_fetch_account();
}

inline void Cv32e40pEvents::jump_fetch_account()
{
    /* The jump is decided in ID, so the instruction fetched behind it is
     * flushed, unless the jump waited in ID after it set the PC. */
    this->jump_bubble = this->jump_slack == 0;
    if (this->jump_bubble)
    {
        this->iss.exec.stall_cycles_inc(1);
    }
    this->jump_slack = 0;
    this->redirect = true;
}

inline void Cv32e40pEvents::hwloop_decode(iss_insn_t *insn)
{
    /* The controller enters DECODE_HWLOOP on an instruction of the loop body,
     * and jumps to the loop start when the instruction before the loop end
     * (get_end()) is in ID. In DECODE, after a trap, its return or a status
     * CSR access, the loop end sets the PC like a jump (cv32e40p_controller.sv,
     * PC_HWLOOP). */
    iss_reg_t pc = insn->addr;
    iss_opcode_t enc = insn->size == 2 ? insn->opcode & 0xFFFF : insn->opcode;
    this->hwloop_jump = false;
    bool system = (enc & 0x7F) == 0x0F || ((enc & 0x7F) == 0x73 && ((enc >> 12) & 7) == 0)
        || status_csr_access(enc, this->iss.exec.debug_mode);
    if ((insn->size == 4 && system) || enc == 0x9002 || (!this->hwloop_state && is_jump(insn)))
    {
        return;
    }
    bool body = false;
    for (int i = 0; i < CONFIG_GVSOC_ISS_NB_HWLOOP; i++)
    {
        body |= this->iss.hwloop.get_count(i) > 1 && this->iss.hwloop.get_start(i) <= pc
            && pc <= this->iss.hwloop.get_end(i);
    }
    if (!this->hwloop_state)
    {
        if (!body)
        {
            return;
        }
        this->hwloop_state = true;
        for (int i = 0; i < CONFIG_GVSOC_ISS_NB_HWLOOP; i++)
        {
            iss_reg_t end = this->iss.hwloop.get_end(i);
            this->hwloop_jump |= pc == end && this->iss.hwloop.get_count(i) > 1;
            // Before a loop end, DECODE_HWLOOP would come too late for the jump.
            this->hwloop_state &= pc + 4 != end;
        }
        return;
    }
    // Loop 0 comes last, so its update wins.
    for (int i = CONFIG_GVSOC_ISS_NB_HWLOOP - 1; i >= 0; i--)
    {
        if (pc + 4 == this->iss.hwloop.get_end(i))
        {
            this->hwloop_state = this->iss.hwloop.get_count(i) > 1 || body;
        }
    }
}

inline void Cv32e40pEvents::event_div_account(iss_reg_t dividend, iss_reg_t divisor,
    bool is_signed, bool is_rem)
{
    /* The serial divider (cv32e40p_alu_div.sv) takes clz(divisor) + 3 cycles,
     * 35 for a zero divisor and clz(~divisor) + 2 for a negative signed one.
     * The ID stage waits for it whatever the next instruction. */
    int32_t d = (int32_t)divisor;
    int cycles;
    if (d == 0)
    {
        cycles = 35;
    }
    else if (is_signed && d < 0)
    {
        uint32_t inv = ~(uint32_t)d;
        cycles = (inv == 0 ? 32 : __builtin_clz(inv)) + 2;
    }
    else
    {
        cycles = __builtin_clz((uint32_t)d) + 3;
    }
    this->iss.exec.stall_cycles_inc(cycles - 1);
    this->ex_extra += cycles - 1;
}

inline void Cv32e40pEvents::event_misaligned_account(int incr)
{
    // The LSU splits an access across a word boundary into two transfers.
    Events::event_misaligned_account(incr);
    this->iss.exec.stall_cycles_inc(incr);
    this->ex_extra += incr;
    this->ex_misaligned = true;
}

inline void Cv32e40pEvents::event_insn_latency_account(iss_insn_t *insn, int latency)
{
    /* Latency set by the core recipe for the multi-cycle multiplier and the
     * fences, which hold the ID stage. */
    this->iss.exec.stall_cycles_inc(latency);
    // This runs after the retire, so ex_slot already holds the multiplier.
    if ((insn->opcode & 0x607F) != 0x000F)
    {
        this->ex_slot.last += latency;
        this->ex_slot.hold_wb = true;
        this->ex_slot.mulh = true;
    }
}

inline void Cv32e40pEvents::event_scoreboard_stall(uint8_t reason)
{
    // First cycle of an instruction waiting for an asynchronous load.
    if (reason == ISS_STALL_REASON_LOAD)
    {
        this->iss.csr.hpm_stall_commit(CV32E40P_HPM_LD_STALL);
    }
}

inline void Cv32e40pEvents::hazards_clear()
{
    this->prev_dest_reg = -1;
    this->prev_load = false;
    this->prev_load_waddr = -1;
    this->wb_load_reg = -1;
}

inline void Cv32e40pEvents::pipeline_flush()
{
    this->hazards_clear();
    this->redirect = false;
    this->after_redirect = true;
    // Drop an instruction held when an interrupt or a debug entry took over.
    this->held_insn = NULL;
    this->hold_cycles = 0;
    this->jump_slack = 0;
    this->hwloop_state = false;
    this->hwloop_jump = false;
    // EX holds bubbles. The FPU results in flight still return.
    this->ex_slot = ExSlot();
    this->ex_prev = ExSlot();
}

inline bool Cv32e40pEvents::status_csr_access(iss_opcode_t enc, bool debug_mode)
{
    // SYSTEM opcode with a CSR funct3 (instr[13:12] != 0).
    if ((enc & 0x7F) != 0x73 || ((enc >> 12) & 0x3) == 0)
    {
        return false;
    }
    // csrrs, csrrc and their immediate forms only read when rs1 or uimm is 0 (CSR_OP_READ).
    bool read = ((enc >> 12) & 0x3) != 0x1 && ((enc >> 15) & 0x1F) == 0;
    uint32_t csr = enc >> 20;
    switch (csr)
    {
        case 0x300: case 0x305: case 0x341: case 0x342:  // mstatus, mtvec, mepc, mcause
        case 0x320:                                      // mcountinhibit
        case 0xCD1:                                      // privlv (read only)
            return true;
        case 0x002: case 0x003:                          // frm, fcsr
            return !read;
        case 0x7B0: case 0x7B1: case 0x7B2: case 0x7B3:  // dcsr, dpc, dscratch0/1
            return debug_mode;
    }
    // mhpmevent3..31, the machine counters and their user read-only mirrors.
    return (csr >= 0x323 && csr <= 0x33F) || (csr & ~0x09Fu) == 0xB00 || (csr & ~0x09Fu) == 0xC00;
}

inline bool Cv32e40pEvents::is_jalr(iss_insn_t *insn)
{
    iss_opcode_t enc = insn->opcode;
    if (insn->size == 2)
    {
        // c.jr and c.jalr: funct4 100x, rs2 = 0, rs1 != 0.
        return (enc & 0xE07F) == 0x8002 && ((enc >> 7) & 0x1F) != 0;
    }
    return (enc & 0x707F) == 0x0067;
}

inline bool Cv32e40pEvents::is_jump(iss_insn_t *insn)
{
    iss_opcode_t enc = insn->opcode;
    if (insn->size == 2)
    {
        // c.j, c.jal (quadrant 1, funct3 101 and 001), c.jr, c.jalr.
        return (enc & 0x6003) == 0x2001 || is_jalr(insn);
    }
    return (enc & 0x7F) == 0x6F || is_jalr(insn);
}

inline bool Cv32e40pEvents::is_lsu(iss_insn_t *insn)
{
    iss_opcode_t enc = insn->opcode;
    if (insn->size == 2)
    {
        // c.lw, c.flw, c.sw, c.fsw and their stack-pointer forms (quadrants 0 and 2).
        uint32_t funct3 = (enc >> 13) & 0x7;
        return (enc & 0x3) != 1 && (funct3 & 0x2) != 0;
    }
    switch (enc & 0x7F)
    {
        case 0x03: case 0x07: case 0x23: case 0x27: case 0x0B:
            return true;
        case 0x2B:
            // CORE-V post-increment stores, and register-register loads and stores.
            return ((enc >> 12) & 0x7) <= 2 || (((enc >> 12) & 0x7) == 3 && (enc >> 25) < 0x18);
    }
    return false;
}

inline bool Cv32e40pEvents::writes_reg(iss_insn_t *insn, int reg)
{
    // The decoder redirects the writes to x0 (Cv32e40pRegfile::set_reg).
    int out = reg == 0 ? ISS_DUMMY_REG : reg;
    for (int i = 0; i < insn->nb_out_reg; i++)
    {
        if (insn->out_regs[i] == out)
        {
            return true;
        }
    }
    return false;
}

inline bool Cv32e40pEvents::reads_reg(iss_insn_t *insn, int reg)
{
    if (reg <= 0)
    {
        return false;
    }
    for (int i = 0; i < insn->nb_in_reg; i++)
    {
        if (insn->in_regs[i] == reg)
        {
            return true;
        }
    }
    return false;
}

inline int Cv32e40pEvents::apu_group(iss_opcode_t enc)
{
    switch (enc & 0x7F)
    {
        case 0x43: case 0x47: case 0x4B: case 0x4F:  // fmadd, fmsub, fnmsub, fnmadd
            return APU_ADDMUL;
        case 0x53:
            switch (enc >> 27)
            {
                case 0x00: case 0x01: case 0x02:     // fadd, fsub, fmul
                    return APU_ADDMUL;
                case 0x03: case 0x0B:                // fdiv, fsqrt
                    return APU_DIVSQRT;
                case 0x18: case 0x1A:                // fcvt.w[u].s, fcvt.s.w[u]
                    return APU_CONV;
            }
            // Sign injection, min/max, comparisons, moves, classify.
            return APU_NONCOMP;
    }
    return -1;
}

inline int Cv32e40pEvents::apu_extra_read(iss_opcode_t enc)
{
    /* The dependency check reads the registers of the operand muxes
     * (apu_read_regs in cv32e40p_id_stage.sv). Operand b is the unused rs2
     * field for fsqrt, fcvt and fclass, an F register without Zfinx, and for
     * the fences and the SYSTEM instructions other than the CSR accesses. It
     * is x1 for c.ebreak. */
    if ((enc & 0xFFFF) == 0x9002)
    {
        return 1;
    }
    if ((enc & 0x7F) == 0x0F || ((enc & 0x7F) == 0x73 && ((enc >> 12) & 0x7) == 0))
    {
        return (enc >> 20) & 0x1F;
    }
    if ((enc & 0x7F) != 0x53)
    {
        return -1;
    }
    uint32_t group = enc >> 27;
    bool fclass = group == 0x1C && ((enc >> 12) & 0x7) == 1;
    if (group != 0x0B && group != 0x18 && group != 0x1A && !fclass)
    {
        return -1;
    }
    int reg = (enc >> 20) & 0x1F;
#if CONFIG_GVSOC_ISS_CV32E40P_ZFINX
    return reg;
#else
    return ISS_NB_REGS + reg;
#endif
}

inline int Cv32e40pEvents::apu_dest(iss_opcode_t enc)
{
    // The comparisons, fcvt.w[u].s, fmv.x.w and fclass write an integer register.
    int rd = (enc >> 7) & 0x1F;
#if CONFIG_GVSOC_ISS_CV32E40P_ZFINX
    return rd;
#else
    uint32_t group = enc >> 27;
    bool int_dest = (enc & 0x7F) == 0x53 && (group == 0x14 || group == 0x18 || group == 0x1C);
    return int_dest ? rd : ISS_NB_REGS + rd;
#endif
}

inline bool Cv32e40pEvents::apu_reads_x0(iss_insn_t *insn)
{
    iss_opcode_t enc = insn->opcode;
    // Operands a and b read x0 when rs1, rs2 or the unused rs2 field is x0.
    for (int i = 0; i < insn->nb_in_reg; i++)
    {
        if (insn->in_regs[i] == 0)
        {
            return true;
        }
    }
    if (apu_extra_read(enc) == 0)
    {
        return true;
    }
    /* Operand c is x0 (REGC_ZERO) except for the stores, fadd and fsub, the
     * fused multiply-adds and the CORE-V instructions that read rd. It is not
     * compared for a JALR, a branch or the CORE-V bit manipulations. */
    if (insn->size == 2)
    {
        uint32_t quadrant = enc & 0x3, funct3 = (enc >> 13) & 0x7;
        bool store = quadrant != 1 && funct3 >= 6;
        bool branch = quadrant == 1 && funct3 >= 6;
        return !store && !branch && !is_jalr(insn);
    }
    uint32_t major = enc & 0x7F, funct3 = (enc >> 12) & 0x7, funct7 = enc >> 25;
    switch (major)
    {
        case 0x23: case 0x27: case 0x63: case 0x67:
        case 0x43: case 0x47: case 0x4B: case 0x4F:
            return false;
        case 0x53:
            return (enc >> 27) > 0x01;
        case 0x2B:
            // Post-increment and register-register stores, bit manipulations.
            if (funct3 <= 2
                || (funct3 == 3 && ((funct7 & 0x10) || (funct7 >= 0x18 && funct7 <= 0x1D))))
            {
                return false;
            }
            break;
        case 0x5B:
            // cv.extract[u], cv.insert, cv.bclr, cv.bset (not cv.bitrev).
            if (funct3 == 0 || (funct3 == 1 && (enc >> 27) != 0x18))
            {
                return false;
            }
            break;
    }
    if (major == 0x0B || major == 0x2B || major == 0x5B || major == 0x7B)
    {
        int rd = (enc >> 7) & 0x1F;
        for (int i = 0; i < insn->nb_in_reg; i++)
        {
            if (insn->in_regs[i] == rd)
            {
                return false;
            }
        }
    }
    return true;
}

inline bool Cv32e40pEvents::apu_reads_reg(iss_insn_t *insn, int reg)
{
    if (reg == 0)
    {
        return apu_reads_x0(insn);
    }
    return reads_reg(insn, reg) || apu_extra_read(insn->opcode) == reg;
}

inline int Cv32e40pEvents::apu_latency(iss_insn_t *insn, int group, int &grant_wait)
{
    grant_wait = 0;
    switch (group)
    {
        case APU_ADDMUL:
            return this->fpu_addmul_lat;
        case APU_NONCOMP: case APU_CONV:
            return this->fpu_others_lat;
    }
    return this->apu_divsqrt_cycles(insn, grant_wait);
}

inline int Cv32e40pEvents::apu_divsqrt_cycles(iss_insn_t *insn, int &grant_wait)
{
    // The operands, read before the instruction executes.
    bool sqrt = (insn->opcode >> 27) == 0x0B;
    uint32_t a = (uint32_t)this->iss.regfile.get_reg_untimed(insn->in_regs[0]);
    uint32_t b = sqrt ? 0 : (uint32_t)this->iss.regfile.get_reg_untimed(insn->in_regs[1]);
    grant_wait = Cv32e40pFdsu::grant_wait(sqrt, a, b);
    return Cv32e40pFdsu::cycles(sqrt, a, b);
}

inline int64_t Cv32e40pEvents::apu_dispatch_bound(iss_insn_t *insn, int64_t &ex, int group,
    int cls, int64_t &jalr_release)
{
    iss_opcode_t enc = insn->opcode;
    bool jalr = is_jalr(insn);
    int rs1 = jalr ? (int)((enc >> (insn->size == 2 ? 7 : 15)) & 0x1F) : -1;

    /* In ID, an instruction that reads a result in flight waits for it
     * (read_dep in cv32e40p_apu_disp.sv), and so does a non-FP instruction
     * that writes that register (write_dep). A JALR waits one cycle more for
     * its target (read_dep_for_jalr, jr_stall_o). */
    for (int i = 0; i < this->apu_nb_ops; i++)
    {
        ApuOp &op = this->apu_ops[i];
        if (op.reg == rs1)
        {
            ex = std::max(ex, op.ret + 2);
            jalr_release = std::max(jalr_release, op.ret + 1);
        }
        else if (apu_reads_reg(insn, op.reg) || (group < 0 && writes_reg(insn, op.reg)))
        {
            ex = std::max(ex, op.ret + 1);
        }
    }
    /* A CSR access waits for an operation of apu_lat 2 or 3 in flight, and
     * for an FP load in EX (cv32e40p_id_stage.sv, csr_apu_stall). */
    if (csr_access(enc))
    {
        ex = std::max(ex, this->apu_active_end + 2);
        if (this->prev_load && this->prev_dest_reg >= ISS_NB_REGS && this->ex_slot.last == ex - 1)
        {
            ex++;
        }
    }

    /* In EX an FP operation waits for the dispatcher while a result is in
     * flight, unless both have apu_lat 2 (stall_type). */
    int64_t accept = ex;
    if (cls > 0 && accept <= this->apu_active_end && (cls != 2 || this->apu_lat_q == 3))
    {
        accept = this->apu_active_end + 1;
    }
    return accept;
}

inline bool Cv32e40pEvents::apu_dep_at(iss_insn_t *insn, int group, int64_t c)
{
    // From the cycle the operation is accepted to the one before its result.
    for (int i = 0; i < this->apu_nb_ops; i++)
    {
        ApuOp &op = this->apu_ops[i];
        if (c >= op.issue && c < op.ret
            && (apu_reads_reg(insn, op.reg) || (group < 0 && writes_reg(insn, op.reg))))
        {
            return true;
        }
    }
    return false;
}

inline void Cv32e40pEvents::apu_events(iss_insn_t *insn, int group, int64_t id_enter,
    int64_t ex_enter, int64_t granted, int64_t accept)
{
    /* The APU lines count once per cycle (cv32e40p_cs_registers.sv). They
     * count the type conflicts of the FP operation in EX without apu_dep, its
     * grant waits, and the cycles the instruction in ID has apu_dep without a
     * grant wait. */
    uint32_t type = 0, dep = 0;
    bool behind = !this->after_redirect;
    for (int64_t c = this->apu_type_start; c < this->apu_type_end; c++)
    {
        type += !(behind && this->apu_dep_at(insn, group, c));
    }
    for (int64_t c = id_enter; c < ex_enter; c++)
    {
        bool nack = c >= this->apu_nack_start && c < this->apu_nack_end;
        dep += this->apu_dep_at(insn, group, c) && !nack;
    }
    for (uint32_t i = 0; i < type; i++)
    {
        this->iss.csr.hpm_stall_commit(CV32E40P_HPM_APU_TYPE);
    }
    for (uint32_t i = 0; i < dep; i++)
    {
        this->iss.csr.hpm_stall_commit(CV32E40P_HPM_APU_DEP);
    }
    for (int64_t c = granted; c < accept; c++)
    {
        this->iss.csr.hpm_stall_commit(CV32E40P_HPM_APU_CONT);
    }
    // The waits of this instruction in EX, for the next one.
    this->apu_type_start = ex_enter;
    this->apu_type_end = granted;
    this->apu_nack_start = granted;
    this->apu_nack_end = accept;
}

inline int Cv32e40pEvents::apu_wb_check(iss_insn_t *insn, int64_t &ex)
{
    /* A multi-cycle result is written on the ALU port (cv32e40p_ex_stage.sv),
     * while ex_slot is in EX. Returns the cycles that instruction waits. */
    int wait = 0;
    while (this->apu_wb >= 0 && this->apu_wb < ex)
    {
        int64_t c = this->apu_wb;
        ExSlot &x = this->ex_slot;
        if (c < x.enter || c > x.last)
        {
            // A bubble in EX.
            this->apu_wb = -1;
            break;
        }
        /* The result waits a cycle in EX (APU_Result_Memorization) behind a
         * misaligned or post-increment access, or a mulh from its second cycle.
         * It also waits behind an ALU port write while a load or store is in
         * WB, or while ID holds a JALR that does not wait for the FPU or the
         * bubble behind one (stale_jalr). */
        bool rvalid = c == x.enter && this->ex_prev.lsu && this->ex_prev.last == c - 1;
        bool jalr_id = x.stale_jalr;
        if (!x.flush && !this->after_redirect && is_jalr(insn))
        {
            int rs1 = (int)((insn->opcode >> (insn->size == 2 ? 7 : 15)) & 0x1F);
            jalr_id = true;
            for (int i = 0; i < this->apu_nb_ops; i++)
            {
                if (this->apu_ops[i].reg == rs1 && this->apu_ops[i].ret >= c)
                {
                    jalr_id = false;
                }
            }
        }
        bool hold = x.hold_wb && !(x.mulh && c == x.enter);
        if (hold || (x.alu_we && (rvalid || jalr_id)))
        {
            // The operation is in flight until its result is written (apu_valid).
            for (int i = 0; i < this->apu_nb_ops; i++)
            {
                if (this->apu_ops[i].ret == c)
                {
                    this->apu_ops[i].ret = c + 1;
                }
            }
            if (this->apu_active_end == c)
            {
                this->apu_active_end = c + 1;
            }
            this->apu_wb = c + 1;
            continue;
        }
        if (x.alu_we && !x.apu)
        {
            // Write port contention (wb_contention). The instruction in EX stays one
            // more cycle if this was its last one.
            this->iss.csr.hpm_stall_commit(CV32E40P_HPM_APU_WB);
            if (c == x.last)
            {
                x.last++;
                ex = std::max(ex, x.last + 1);
                wait = 1;
            }
        }
        this->apu_wb = -1;
    }
    return wait;
}

inline bool Cv32e40pEvents::dispatch_stall(iss_insn_t *insn)
{
    if (insn == this->held_insn)
    {
        if (this->hold_cycles == 0)
        {
            return false;
        }
        this->hold_cycles--;
        return true;
    }
    if (insn->addr != this->next_pc)
    {
        // An interrupt, a debug entry or a trap flushed the pipeline.
        this->pipeline_flush();
    }
    this->hwloop_decode(insn);

    int64_t now = this->iss.clock.get_cycles();
    int64_t ex = now;
    // Cycle this instruction enters ID, and the cycle a jump sets the PC.
    int64_t id_enter = (this->ex_slot.flush || this->ex_slot.enter < 0) ? now - 1
                                                                        : this->ex_slot.enter;
    int64_t jalr_release = id_enter;
    uint32_t events = 0;
    int cycles = 0;
    bool jalr = is_jalr(insn);
    // The instruction before may wait in EX for the write port.
    int wb_wait = this->apu_wb_check(insn, ex);
    if (this->after_redirect)
    {
        /* A 32-bit target at a halfword address needs a second fetch, which a
         * jump waiting in ID does not hide. A jump waiting in EX for the write
         * port holds the target in IF (cv32e40p_if_stage.sv, id_ready). */
        cycles = (insn->size == 4 && (insn->addr & 2)) + wb_wait;
        /* The bubble behind a JALR that writes its target register sees that
         * JALR as its producer (jr_stall_o), so the target waits a cycle in IF. */
        if (this->ex_slot.self_jalr)
        {
            cycles++;
            events = CV32E40P_HPM_JR_STALL;
        }
        id_enter = now + cycles - 1;
        jalr_release = id_enter;
        ex = std::max(ex, now + cycles);
        cycles = 0;
    }
    else
    {
        bool reads_prev = reads_reg(insn, this->prev_dest_reg);
        /* load_stall_o is also raised when the instruction in ID writes the
         * destination of the load in EX on the ALU port (cv32e40p_controller.sv).
         * An LSU instruction writes that port only on its address register. */
        iss_opcode_t enc = insn->size == 2 ? insn->opcode & 0xFFFF : insn->opcode;
        bool waw = this->prev_load_waddr >= 0 && !is_lsu(insn) && alu_port_write(insn, enc, false)
            && rtl_reg(insn->nb_out_reg > 0 ? insn->out_regs[0] : 0) == this->prev_load_waddr;
        if (reads_prev && this->prev_load)
        {
            /* Load-use stall (load_stall_o). A JALR also waits for the load in
             * WB (jr_stall_o), and the event lines fire on the first cycle only. */
            cycles = jalr ? 2 : 1;
            events = CV32E40P_HPM_LD_STALL | (jalr ? CV32E40P_HPM_JR_STALL : 0);
        }
        else if (waw)
        {
            // A JALR that reads a load in WB waits in the same cycle (jr_stall_o).
            cycles = 1;
            events = CV32E40P_HPM_LD_STALL
                | (jalr && reads_reg(insn, this->wb_load_reg) ? CV32E40P_HPM_JR_STALL : 0);
        }
        else if (jalr && (reads_prev || reads_reg(insn, this->wb_load_reg)))
        {
            // The producer of the JALR rs1 is in EX, or is a load in WB (jr_stall_o).
            cycles = 1;
            events = CV32E40P_HPM_JR_STALL;
        }
        if (jalr)
        {
            if (reads_prev)
            {
                jalr_release = this->ex_slot.last + (this->prev_load ? 2 : 1);
            }
            else if (reads_reg(insn, this->wb_load_any))
            {
                jalr_release = id_enter + 1;
            }
        }
    }
    this->jump_slack = 0;
    ex += cycles;

    int group = apu_group(insn->opcode);
    int latency = 0, cls = 0, grant_wait = 0;
    if (group >= 0)
    {
        // RTL apu_lat (cv32e40p_decoder.sv).
        latency = this->apu_latency(insn, group, grant_wait);
        cls = (group == APU_DIVSQRT || latency >= 2) ? 3 : latency + 1;
    }
    // The divider may grant the request a cycle late (stall_nack).
    int64_t granted = this->apu_dispatch_bound(insn, ex, group, cls, jalr_release);
    int64_t accept = granted + grant_wait;
    this->apu_events(insn, group, id_enter, ex, granted, accept);
    this->ex_enter = ex;
    this->ex_enter_insn = insn;
    if (group >= 0)
    {
        this->apu_insn = insn;
        this->apu_cls = cls;
        this->apu_enter = ex;
        this->apu_issue = accept;
        this->apu_ret = accept + latency;
    }

    if (is_jump(insn) || this->hwloop_jump)
    {
        /* The jump sets the PC in ID once its target is known (jump_in_dec).
         * If it then waits in ID, the fetch of the target goes on. */
        this->jump_slack = (int)std::max((int64_t)0, accept - 1 - std::max(id_enter, jalr_release));
    }

    int64_t total = accept - now;
    if (events)
    {
        this->iss.csr.hpm_stall_commit(events);
    }
    if (total == 0)
    {
        return false;
    }
    this->held_insn = insn;
    this->hold_cycles = (int)total - 1;
    return true;
}

inline void Cv32e40pEvents::event_retire_account(iss_insn_t *insn)
{
    Events::event_retire_account(insn);

    bool held = this->held_insn == insn;
    this->held_insn = NULL;
    this->hold_cycles = 0;
    this->next_pc = this->iss.exec.current_insn;

    // A trapping instruction does not retire, so its event lines are dropped.
    if (this->iss.exec.has_exception)
    {
        this->pending_events = 0;
        this->apu_insn = NULL;
        this->ex_extra = 0;
        this->ex_misaligned = false;
        this->pipeline_flush();
        if (this->iss.exec.cosim->enabled())
        {
            this->iss.exec.cosim->retire(insn);
        }
        return;
    }

    // A compressed opcode carries the next parcel in its upper half.
    iss_reg_t enc = (insn->size == 2) ? (insn->opcode & 0xFFFF) : insn->opcode;

    /* The minstret and compressed lines exclude ebreak
     * (cv32e40p_id_stage.sv). This covers the ebreak that enters debug mode,
     * which does not trap. */
    bool count_instr = !(enc == 0x00100073u
                         || (insn->size == 2 && enc == 0x9002u));
    uint32_t events = this->pending_events
        | (count_instr ? (CV32E40P_HPM_INSTR
            | (insn->size == 2 ? CV32E40P_HPM_COMP_INSTR : 0)) : 0);
    this->pending_events = 0;
    this->iss.csr.hpm_commit(events, count_instr);

    bool flush = false;
    if (insn->size == 4 && status_csr_access(enc, this->iss.exec.debug_mode))
    {
        // Flush behind a status CSR access, with the next instruction held in IF.
        this->iss.exec.stall_cycles_inc(3);
        flush = true;
    }
    else if (enc == 0x30200073u || enc == 0x7B200073u)
    {
        // mret and dret go through FLUSH_EX, FLUSH_WB and XRET_JUMP, then fetch from mepc or dpc.
        this->iss.exec.stall_cycles_inc(4);
        this->redirect = true;
    }
    else if ((enc & 0x607F) == 0x000F)
    {
        /* After fence and fence.i, the next instruction is fetched again after
         * the flush (PC_FENCEI), within the latency set by the core recipe. */
        this->redirect = true;
    }
    if (flush || (insn->size == 4 && ((enc & 0x7F) == 0x0F
        || ((enc & 0x7F) == 0x73 && ((enc >> 12) & 7) == 0))))
    {
        // The flushes, mret, dret and wfi end in DECODE.
        this->hwloop_state = false;
    }
    if (this->hwloop_jump)
    {
        // The loop end set the PC in ID (hwloop_decode).
        this->hwloop_jump = false;
        this->jump_fetch_account();
    }

    // Hazard state for the next dispatch.
    int wb_load = (this->prev_load && !held) ? this->prev_dest_reg : -1;
    this->after_redirect = this->redirect;
    this->redirect = false;
    if (this->after_redirect || flush)
    {
        this->hazards_clear();
    }
    else
    {
        // x1-x31, and f0-f31 for the FP loads.
        bool dest = insn->nb_out_reg > 0 && insn->out_regs[0] > 0
            && insn->out_regs[0] < ISS_NB_REGS + ISS_NB_FREGS;
        this->prev_dest_reg = dest ? insn->out_regs[0] : -1;
        this->prev_load = dest && (events & CV32E40P_HPM_LD) != 0;
        bool load = insn->nb_out_reg > 0 && (events & CV32E40P_HPM_LD) != 0;
        this->prev_load_waddr = load ? rtl_reg(insn->out_regs[0]) : -1;
        // insn->latency is charged after this hook, the other costs before.
        bool single = this->iss.exec.pending_stall_cycles() == 0 && insn->latency == 0;
        this->wb_load_reg = single ? wb_load : -1;
        this->wb_load_any = wb_load;
    }

    // The instruction in EX, for the write port of the FPU results.
    bool lsu = (events & (CV32E40P_HPM_LD | CV32E40P_HPM_ST)) != 0;
    int64_t now = this->iss.clock.get_cycles();
    this->ex_prev = this->ex_slot;
    ExSlot &x = this->ex_slot;
    x.enter = this->ex_enter_insn == insn ? this->ex_enter : now;
    x.last = now + this->ex_extra;
    x.lsu = lsu;
    x.apu = apu_group(enc) >= 0;
    x.alu_we = alu_port_write(insn, enc, lsu);
    x.hold_wb = this->ex_misaligned || (lsu && x.alu_we);
    x.mulh = false;
    x.flush = flush || this->after_redirect;
    x.stale_jalr = is_jalr(insn) && this->jump_bubble;
    if (x.stale_jalr)
    {
        // c.jalr links in ra, c.jr in x0.
        int rd = insn->size == 2 ? (enc >> 12) & 0x1 : (enc >> 7) & 0x1F;
        int rs1 = (enc >> (insn->size == 2 ? 7 : 15)) & 0x1F;
        x.self_jalr = rd != 0 && rd == rs1;
    }
    else
    {
        x.self_jalr = false;
    }
    this->ex_extra = 0;
    this->ex_misaligned = false;
    this->ex_enter_insn = NULL;
    this->apu_retire(insn);

    // After hpm_commit, so that the record has the new minstret.
    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->retire(insn);
    }
}

inline bool Cv32e40pEvents::alu_port_write(iss_insn_t *insn, iss_opcode_t enc, bool lsu)
{
    // regfile_alu_we of the RTL decoder, rd = x0 included.
    uint32_t major = enc & 0x7F;
    uint32_t funct3 = (enc >> 12) & 0x7;
    if (lsu)
    {
        // The CORE-V post-increment loads and stores write rs1 back.
        return insn->size == 4 && ((major == 0x0B && funct3 != 3)
            || (major == 0x2B && (funct3 <= 2 || (funct3 == 3 && !((enc >> 27) & 1)))));
    }
    if (insn->size == 2)
    {
        // All but c.beqz, c.bnez and c.ebreak.
        return (enc & 0xC003) != 0xC001 && enc != 0x9002;
    }
    switch (major)
    {
        case 0x13: case 0x33: case 0x37: case 0x17: case 0x6F: case 0x67:
            return true;
        case 0x73:
            return funct3 != 0;
        case 0x63: case 0x0F:
            return false;
        case 0x43: case 0x47: case 0x4B: case 0x4F: case 0x53:
            // The FP operations write through the APU path of EX (apu_waddr).
            return false;
    }
    // CORE-V: the instructions with a destination register.
    return insn->nb_out_reg > 0;
}

inline void Cv32e40pEvents::apu_retire(iss_insn_t *insn)
{
    if (insn != this->apu_insn)
    {
        return;
    }
    this->apu_insn = NULL;
    this->apu_lat_q = this->apu_cls;
    if (this->apu_cls < 2)
    {
        // The result returns in the cycle of the operation.
        return;
    }
    // Drop the results no rule needs any more (see apu_dispatch_bound).
    int n = 0;
    for (int i = 0; i < this->apu_nb_ops; i++)
    {
        if (this->apu_ops[i].ret + 2 > this->apu_enter)
        {
            this->apu_ops[n++] = this->apu_ops[i];
        }
    }
    if (n < APU_MAX_OPS)
    {
        this->apu_ops[n++] = { apu_dest(insn->opcode), this->apu_issue, this->apu_ret };
    }
    this->apu_nb_ops = n;
    this->apu_active_end = this->apu_ret;
    if (this->apu_cls == 3)
    {
        this->apu_wb = this->apu_ret;
    }
}

inline void Cv32e40pEvents::insn_stall_account()
{
    Events::insn_stall_account();

    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->drain();
    }
}
