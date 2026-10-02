// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

/* Bits of the RTL hpm_events (cv32e40p_cs_registers.sv). The cycle line
 * comes from the clock (Cv32e40pCsr::hpm_cycle_fold). imiss and the ELW
 * stall never fire, because there are no fetch wait states and no event
 * unit. */
#define CV32E40P_HPM_INSTR        (1u << 1)
#define CV32E40P_HPM_LD_STALL     (1u << 2)
#define CV32E40P_HPM_JR_STALL     (1u << 3)
#define CV32E40P_HPM_LD           (1u << 5)
#define CV32E40P_HPM_ST           (1u << 6)
#define CV32E40P_HPM_JUMP         (1u << 7)
#define CV32E40P_HPM_BRANCH       (1u << 8)
#define CV32E40P_HPM_BRANCH_TAKEN (1u << 9)
#define CV32E40P_HPM_COMP_INSTR   (1u << 10)
#define CV32E40P_HPM_APU_TYPE     (1u << 12)
#define CV32E40P_HPM_APU_CONT     (1u << 13)
#define CV32E40P_HPM_APU_DEP      (1u << 14)
#define CV32E40P_HPM_APU_WB       (1u << 15)

/* Event lines and pipeline timing of the CV32E40P, for a memory without
 * wait states such as the cv32e40p_testbench memory.
 *
 * A cost the RTL pays after an instruction (taken branch, jump, divider,
 * multi-cycle multiplier, flush, misaligned access, trap entry and return)
 * is charged as stall cycles behind it, as for Ri5ky. A stall the RTL takes
 * with the instruction in ID (load-use, jump register, a 32-bit jump target
 * at a halfword address) holds it at dispatch (dispatch_stall).
 *
 * The FPU is behind the APU interface (cv32e40p_apu_disp.sv). A result
 * returns FPU_ADDMUL_LAT or FPU_OTHERS_LAT cycles after the operation is
 * accepted in EX, or after the divider iterations. The instructions behind it
 * wait at dispatch for the result, for the dispatcher and for the write port
 * (apu_dispatch_bound, apu_wb_check). */
class Cv32e40pEvents : public Events
{
public:
    // Reads the FPU latencies from the core configuration.
    Cv32e40pEvents(Iss &iss);

    void reset(bool active);

    inline void event_load_account(int incr);
    inline void event_store_account(int incr);
    inline void event_branch_account();
    inline void event_taken_branch_account();
    inline void event_jump_account();
    inline void event_jalr_account(int rs1);
    inline void event_div_account(iss_reg_t dividend, iss_reg_t divisor, bool is_signed,
        bool is_rem);
    inline void event_misaligned_account(int incr);
    inline void event_insn_latency_account(iss_insn_t *insn, int latency);
    inline void event_scoreboard_stall(uint8_t reason);
    inline void event_retire_account(iss_insn_t *insn);
    // Called for each instruction leaving the commit FIFO (ExecInOrder::drain_entry).
    inline void insn_stall_account();

    /* Called at the dispatch of an instruction
     * (Cv32e40pRegfile::scoreboard_insn_check) and every cycle it is held.
     * Returns true to hold it one more cycle in ID. */
    inline bool dispatch_stall(iss_insn_t *insn);
    // The instruction waits for an asynchronous load, which covers the load-use cycle.
    inline void dispatch_held(iss_insn_t *insn) { this->held_insn = insn; }

    /* Cycles the trap entry adds for the instruction enc: 3 through FLUSH_EX
     * and FLUSH_WB (cv32e40p_controller.sv). An illegal instruction enters EX
     * as a bubble and takes one more, unless it is a CSR access, which keeps
     * csr_access. There is no other synchronous exception. */
    static inline int trap_entry_cycles(int id, iss_opcode_t enc)
    {
        switch (id)
        {
            case ISS_EXCEPT_ILLEGAL:
                return csr_access(enc) ? 3 : 4;
            case ISS_EXCEPT_BREAKPOINT:
            case ISS_EXCEPT_ENV_CALL_U_MODE:
            case ISS_EXCEPT_ENV_CALL_M_MODE:
                return 3;
        }
        return 0;
    }

private:
    /* Event lines fired by the executing instruction, counted at its retire,
     * each once at most as in the RTL (cv32e40p_cs_registers.sv). The LSU
     * fires its hooks for accepted requests only, so a retry adds nothing. */
    uint32_t pending_events = 0;

    /* A CSR access that the decoder flags with csr_status_o. The controller
     * flushes the pipeline behind it. */
    static inline bool status_csr_access(iss_opcode_t enc, bool debug_mode);
    // RTL csr_access_o: SYSTEM opcode with a CSR funct3.
    static inline bool csr_access(iss_opcode_t enc)
    {
        return (enc & 0x7F) == 0x73 && ((enc >> 12) & 0x7) != 0;
    }
    static inline bool is_jalr(iss_insn_t *insn);
    static inline bool is_jump(iss_insn_t *insn);
    static inline bool is_lsu(iss_insn_t *insn);
    // RTL 6-bit address of a register of the model, x0 included.
    static inline int rtl_reg(int reg) { return reg == ISS_DUMMY_REG ? 0 : reg; }
    static inline bool reads_reg(iss_insn_t *insn, int reg);
    static inline bool writes_reg(iss_insn_t *insn, int reg);
    inline void pipeline_flush();
    inline void hazards_clear();
    inline void jump_fetch_account();
    // Controller state of the hardware loops for the instruction in ID.
    inline void hwloop_decode(iss_insn_t *insn);

    /* FPU operation groups of the decoder (fp_op_group), -1 for an
     * instruction that does not use the APU. */
    enum { APU_ADDMUL, APU_DIVSQRT, APU_NONCOMP, APU_CONV };
    static inline int apu_group(iss_opcode_t enc);
    // Register compared with the results in flight besides the operands, -1 for none.
    static inline int apu_extra_read(iss_opcode_t enc);
    // RTL 6-bit address of the destination of an FP operation.
    static inline int apu_dest(iss_opcode_t enc);
    // The APU dependency check compares x0 like any other register.
    static inline bool apu_reads_x0(iss_insn_t *insn);
    static inline bool apu_reads_reg(iss_insn_t *insn, int reg);
    // Cycles from the acceptance of an FPU operation to its result, and its grant wait.
    inline int apu_latency(iss_insn_t *insn, int group, int &grant_wait);
    inline int64_t apu_dispatch_bound(iss_insn_t *insn, int64_t &ex, int group, int cls,
        int64_t &jalr_release);
    inline int apu_wb_check(iss_insn_t *insn, int64_t &ex);
    inline void apu_retire(iss_insn_t *insn);
    // apu_dep of the instruction in ID in cycle c (cv32e40p_apu_disp.sv).
    inline bool apu_dep_at(iss_insn_t *insn, int group, int64_t c);
    inline void apu_events(iss_insn_t *insn, int group, int64_t id_enter, int64_t ex_enter,
        int64_t granted, int64_t accept);
    // Latency and grant wait of fdiv and fsqrt (Cv32e40pFdsu).
    inline int apu_divsqrt_cycles(iss_insn_t *insn, int &grant_wait);
    static inline bool alu_port_write(iss_insn_t *insn, iss_opcode_t enc, bool lsu);

    /* PC after the last retire. A dispatch at another PC follows an
     * interrupt, a trap or a debug entry. */
    iss_reg_t next_pc = 0;
    // Destination of the last retired instruction (-1 for none), and whether it is a load.
    int prev_dest_reg = -1;
    bool prev_load = false;
    // RTL address of the destination of that load, x0 included, or -1.
    int prev_load_waddr = -1;
    /* Destination of a load that is still in WB when the next instruction is
     * in ID, or -1. The load is two instructions before, with a single-cycle
     * one in between. Only a JALR waits for it. */
    int wb_load_reg = -1;
    /* The same load whatever the instruction in between. A jump in ID sets
     * the PC once the load leaves WB. */
    int wb_load_any = -1;
    // Set by a taken branch or a jump: the next dispatch is the target.
    bool redirect = false;
    bool after_redirect = false;
    // Instruction held for its stall until it retires, and the cycles left.
    iss_insn_t *held_insn = NULL;
    int hold_cycles = 0;
    /* Cycles a jump waited in ID after it set the PC, behind a multi-cycle
     * instruction in EX: they hide the fetch of its target. */
    int jump_slack = 0;
    // The last jump left a bubble in ID.
    bool jump_bubble = false;
    /* hwloop_state is set while the controller is in DECODE_HWLOOP.
     * hwloop_jump is set when the instruction in ID is a loop end decoded in
     * DECODE, which sets the PC to the loop start like a jump. */
    bool hwloop_state = false;
    bool hwloop_jump = false;

    /* Instruction in EX and the one before, for the write port of the FPU
     * results. Each slot keeps its cycles in EX, from enter to last, and what
     * the instruction does there. */
    struct ExSlot
    {
        int64_t enter = -1;
        int64_t last = -1;
        bool alu_we = false;      // writes on the ALU port (regfile_alu_we)
        bool apu = false;
        bool lsu = false;
        bool hold_wb = false;     // misaligned access, post-increment access or mulh
        bool mulh = false;        // holds the results from its second cycle
        bool flush = false;       // no instruction behind it enters ID meanwhile
        bool stale_jalr = false;  // JALR whose encoding stays in ID in the bubble behind it
        bool self_jalr = false;   // and the JALR writes its target register (rd = rs1 != x0)
    };
    ExSlot ex_slot;
    ExSlot ex_prev;
    /* Cycle the instruction dispatching now enters EX, and the cycles it adds
     * there (divider, multiplier, misaligned access). */
    int64_t ex_enter = -1;
    iss_insn_t *ex_enter_insn = NULL;
    int ex_extra = 0;
    bool ex_misaligned = false;

    // RTL FPU_ADDMUL_LAT and FPU_OTHERS_LAT.
    int fpu_addmul_lat = 0;
    int fpu_others_lat = 0;
    /* Results in flight (inflight and waiting in cv32e40p_apu_disp.sv), with
     * the RTL 6-bit register address, the cycle the operation is accepted and
     * the cycle its result returns. */
    struct ApuOp
    {
        int reg;
        int64_t issue;
        int64_t ret;
    };
    static constexpr int APU_MAX_OPS = 4;
    ApuOp apu_ops[APU_MAX_OPS];
    int apu_nb_ops = 0;
    /* apu_lat of the last accepted operation, and the last cycle the
     * dispatcher is active (an operation of apu_lat 2 or 3 in flight). */
    int apu_lat_q = 0;
    int64_t apu_active_end = -1;
    // Cycle a multi-cycle result is due on the ALU write port, or -1.
    int64_t apu_wb = -1;
    /* Cycles [start, end) the last FP operation waited in EX for the
     * dispatcher (stall_type) and for the grant (stall_nack). The type line
     * is counted with the instruction in ID behind it. */
    int64_t apu_type_start = 0;
    int64_t apu_type_end = 0;
    int64_t apu_nack_start = 0;
    int64_t apu_nack_end = 0;
    // Operation accepted at the last dispatch, recorded at its retire.
    iss_insn_t *apu_insn = NULL;
    int apu_cls = 0;
    int64_t apu_enter = 0;
    int64_t apu_issue = 0;
    int64_t apu_ret = 0;
};
