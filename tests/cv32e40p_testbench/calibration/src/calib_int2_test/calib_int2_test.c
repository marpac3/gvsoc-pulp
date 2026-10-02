// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// RV32IMC timing, the hazards and the traps that calib_int_test leaves out. Blocks are 32-bit
// (norvc) unless named c_*.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"
#define RVC(s)   ".option push\n\t.option rvc\n\t" s "\n\t.option pop"

// jalr whose rs1 comes from a load: the target address goes through calib_buf[0], with N
// instructions between the load and the jalr.
#define JALR_LW(n) "la t2, 1f\n\tsw t2, 0(%[buf])\n\tlw t0, 0(%[buf])\n\t" n "jalr x0, 0(t0)\n1:"
// The same groups without the jalr (falls through to 1f).
#define LA_SW_LW   "la t2, 1f\n\tsw t2, 0(%[buf])\n\tlw t0, 0(%[buf])\n\tnop\n1:"
// rs1 of the jalr written one instruction before it, then two.
#define JALR_DIST1 "auipc t0, 0\n\taddi t0, t0, 16\n\tnop\n\tjalr x0, 0(t0)"
// c.jr with rs1 two instructions before it, or right before it. A c.nop after it keeps the next
// group word aligned.
#define C_JR_NODEP NORVC("auipc t0, 0\n\taddi t0, t0, 20\n\tnop\n\tnop") "\n\t" \
                   RVC("c.jr t0\n\tc.nop")
#define C_JR_DEP   NORVC("auipc t0, 0\n\tnop\n\tnop\n\taddi t0, t0, 20") "\n\t" \
                   RVC("c.jr t0\n\tc.nop")
// 22-byte groups: the jalr target is at a halfword address in every other group.
#define JALR_ODD   NORVC("auipc t0, 0\n\taddi t0, t0, 22\n\tnop\n\tnop\n\tjalr x0, 0(t0)") "\n\t" \
                   RVC("c.nop")
#define JALR_DEP   "auipc t0, 0\n\tnop\n\tnop\n\taddi t0, t0, 20\n\tjalr x0, 0(t0)"

// A trap handler at a 256-byte aligned address (mtvec base), skipping the 4-byte trapping
// instruction. mtvec is saved in a5 and restored by TRAP_POST.
#define TRAP_PRE \
  "j 2f\n\t.p2align 8\n3:\n\tcsrr a3, 0x341\n\taddi a3, a3, 4\n\tcsrw 0x341, a3\n\tmret\n" \
                  "2:\n\tcsrr a5, 0x305\n\tla a4, 3b\n\tcsrw 0x305, a4"
#define TRAP_POST "\n\tcsrw 0x305, a5"

int main(void)
{
  calib_init();
  // calib_buf[2] holds its own address (address operands from a load).
  calib_buf[2] = (uint32_t)(uintptr_t)calib_buf;

  // jalr after a load.
  CALIB_BLOCK("la_sw_lw", 32, "", NORVC(REPT(32, LA_SW_LW)));
  CALIB_BLOCK("jalr_lw0", 32, "", NORVC(REPT(32, JALR_LW(""))));
  CALIB_BLOCK("jalr_lw1", 32, "", NORVC(REPT(32, JALR_LW("nop\n\t"))));
  CALIB_BLOCK("jalr_lw2", 32, "", NORVC(REPT(32, JALR_LW("nop\n\tnop\n\t"))));
  CALIB_BLOCK("jalr_dist1", 32, "", NORVC(REPT(32, JALR_DIST1)));
  CALIB_BLOCK("c_jr_nodep", 32, "", REPT(32, C_JR_NODEP));
  CALIB_BLOCK("c_jr_dep", 32, "", REPT(32, C_JR_DEP));

  // Jump targets at halfword addresses: jalr (every other group), c.j to a 32-bit instruction,
  // and sequential 32-bit instructions at halfword addresses (no jump).
  CALIB_BLOCK("jalr_odd", 32, "", REPT(32, JALR_ODD));
  CALIB_BLOCK("c_j_to_odd32", 32, "",
              RVC("c.nop\n\t" REPT(32, "c.j 1f\n\tc.nop\n1:\n\t" NORVC("addi t1, t1, 0"))));
  CALIB_BLOCK("seq_odd32", 32, "", RVC("c.nop") "\n\t" NORVC(REPT(32, "addi t1, t1, 1")));

  // Load-use on the other operand readers.
  CALIB_BLOCK("branch_lw_use_taken", 32, "",
              NORVC(REPT(32, "lw t0, 0(%[buf])\n\tbeq t0, t0, 1f\n1:")));
  CALIB_BLOCK("branch_lw_use_not", 32, "",
              NORVC(REPT(32, "lw t0, 0(%[buf])\n\tbne t0, t0, 1f\n1:")));
  CALIB_BLOCK("sw_data_lw_use", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tsw t0, 4(%[buf])")));
  CALIB_BLOCK("sw_addr_lw_use", 32, "", NORVC(REPT(32, "lw t0, 8(%[buf])\n\tsw t1, 12(t0)")));
  CALIB_BLOCK("lw_addr_lw_use", 32, "", NORVC(REPT(32, "lw t0, 8(%[buf])\n\tlw t1, 12(t0)")));
  CALIB_BLOCK("sw_lw", 32, "", NORVC(REPT(32, "sw t1, 0(%[buf])\n\tlw t0, 0(%[buf])")));
  CALIB_BLOCK("mul_lw_use", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tmul t1, t0, t0")));
  CALIB_BLOCK("div_lw_use", 16, "li t2, 100",
              NORVC(REPT(16, "lw t0, 28(%[buf])\n\tdiv t1, t2, t0")));
  CALIB_BLOCK("csrw_lw_use", 16, "", NORVC(REPT(16, "lw t0, 0(%[buf])\n\tcsrw 0x340, t0")));

  // Back to back multi-cycle units, and a jump whose link register the target reads.
  CALIB_BLOCK("mulh_mulh", 16, "li t1, 3\n\tli t2, 5",
              NORVC(REPT(16, "mulh t0, t1, t2\n\tmulh t3, t1, t2")));
  CALIB_BLOCK("div_div_dep", 16, "li t1, 100\n\tli t2, 7",
              NORVC(REPT(16, "div t0, t1, t2\n\tdiv t3, t0, t2")));
  CALIB_BLOCK("mul_div", 16, "li t1, 100\n\tli t2, 7",
              NORVC(REPT(16, "mul t0, t1, t2\n\tdiv t3, t0, t2")));
  CALIB_BLOCK("jal_ra_use", 32, "", NORVC(REPT(32, "jal ra, 1f\n1:\n\taddi t1, ra, 0")));
  CALIB_BLOCK("csrw_csrr_mscratch", 32, "", NORVC(REPT(32, "csrw 0x340, t0\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("csrr_mcycle_use", 32, "", NORVC(REPT(32, "csrr t0, 0xB00\n\taddi t1, t0, 1")));
  CALIB_BLOCK("loop16", 16, "li t3, 16", NORVC("1:\n\taddi t3, t3, -1\n\tbne t3, x0, 1b"));

  // Traps: ecall and ebreak to a local handler (csrr mepc, addi, csrw mepc, mret), an illegal
  // instruction, and mret alone (mepc set to the next instruction, mstatus saved and restored).
  CALIB_BLOCK_POST("ecall_mret", 16, TRAP_PRE, NORVC(REPT(16, "ecall")), TRAP_POST);
  CALIB_BLOCK_POST("ebreak_mret", 16, TRAP_PRE, NORVC(REPT(16, "ebreak")), TRAP_POST);
  CALIB_BLOCK_POST("illegal_mret", 16, TRAP_PRE, NORVC(REPT(16, ".word 0x00000000")), TRAP_POST);
  CALIB_BLOCK_POST("mret_only", 16, "csrr a5, 0x300",
                   NORVC(REPT(16, "la t0, 1f\n\tcsrw 0x341, t0\n\tmret\n1:")),
                   "\n\tcsrw 0x300, a5");
  CALIB_BLOCK("la_csrw_mepc", 16, "", NORVC(REPT(16, "la t0, 1f\n\tcsrw 0x341, t0\n\tnop\n1:")));

  // Stall events and the jump event of a held jalr (mhpmevent3: jump 0x80, jr_stall 0x08,
  // instr 0x02, ld_stall 0x04).
  CALIB_HPM("jalr_dep", 0x80, "", NORVC(REPT(32, JALR_DEP)));
  CALIB_HPM("jalr_dep", 0x88, "", NORVC(REPT(32, JALR_DEP)));
  CALIB_HPM("lw_use", 0x06, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\taddi t1, t0, 1")));
  CALIB_HPM("jalr_lw0", 0x0C, "", NORVC(REPT(32, JALR_LW(""))));
  CALIB_HPM("jalr_lw1", 0x0C, "", NORVC(REPT(32, JALR_LW("nop\n\t"))));
  CALIB_HPM("jalr_dist1", 0x08, "", NORVC(REPT(32, JALR_DIST1)));
  CALIB_HPM("branch_lw_use_taken", 0x04, "",
            NORVC(REPT(32, "lw t0, 0(%[buf])\n\tbeq t0, t0, 1f\n1:")));
  CALIB_HPM("sw_data_lw_use", 0x04, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tsw t0, 4(%[buf])")));

  return 0;
}
