// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// RV32IMC timing: ALU, branches, jumps, loads and stores, multiplier, divider, CSR accesses and
// fences. Blocks are 32-bit (norvc) unless named c_*.

#include "calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"
#define RVC(s)   ".option push\n\t.option rvc\n\t" s "\n\t.option pop"

// auipc, addi (target 20 bytes on), two filler instructions, jalr: rs1 two instructions before the
// jalr, or written by the instruction right before it (jump register hazard).
#define JALR_NODEP "auipc t0, 0\n\taddi t0, t0, 20\n\tnop\n\tnop\n\tjalr x0, 0(t0)"
#define JALR_DEP   "auipc t0, 0\n\tnop\n\tnop\n\taddi t0, t0, 20\n\tjalr x0, 0(t0)"

int main(void)
{
  calib_init();

  CALIB_BLOCK("empty", 0, "", "");
  CALIB_BLOCK("nop", 64, "", NORVC(REPT(64, "nop")));
  CALIB_BLOCK("c_nop", 64, "", RVC(REPT(64, "c.nop")));
  CALIB_BLOCK("addi_dep", 64, "", NORVC(REPT(64, "addi t0, t0, 1")));

  // Control transfers.
  CALIB_BLOCK("branch_taken", 64, "", NORVC(REPT(64, "beq x0, x0, 1f\n1:")));
  CALIB_BLOCK("branch_taken_misaligned", 64, "", RVC("c.nop") "\n\t"
                                                 NORVC(REPT(64, "beq x0, x0, 1f\n1:")));
  CALIB_BLOCK("branch_not_taken", 64, "", NORVC(REPT(64, "bne x0, x0, 1f\n1:")));
  CALIB_BLOCK("branch_taken_dep", 64, "li t1, 1",
              NORVC(REPT(64, "addi t1, t1, 0\n\tbne t1, x0, 1f\n1:")));
  CALIB_BLOCK("jal", 64, "", NORVC(REPT(64, "jal x0, 1f\n1:")));
  CALIB_BLOCK("jal_misaligned", 64, "", RVC("c.nop") "\n\t" NORVC(REPT(64, "jal x0, 1f\n1:")));
  CALIB_BLOCK("c_j", 64, "", RVC(REPT(64, "c.j 1f\n1:")));
  CALIB_BLOCK("jalr_nodep", 32, "", NORVC(REPT(32, JALR_NODEP)));
  CALIB_BLOCK("jalr_dep", 32, "", NORVC(REPT(32, JALR_DEP)));

  // Loads and stores (calib_buf, no memory stalls on the RTL run).
  CALIB_BLOCK("lw", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])")));
  CALIB_BLOCK("lw_use", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\taddi t1, t0, 1")));
  CALIB_BLOCK("lw_nouse", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\taddi t1, t2, 1")));
  CALIB_BLOCK("lw_use_after_one", 32, "",
              NORVC(REPT(32, "lw t0, 0(%[buf])\n\tnop\n\taddi t1, t0, 1")));
  CALIB_BLOCK("sw", 32, "", NORVC(REPT(32, "sw t0, 0(%[buf])")));
  CALIB_BLOCK("lw_sw", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tsw t1, 4(%[buf])")));
  CALIB_BLOCK("lw_misaligned", 32, "addi a2, %[buf], 1", NORVC(REPT(32, "lw t0, 0(a2)")));
  CALIB_BLOCK("lh_misaligned", 32, "addi a2, %[buf], 3", NORVC(REPT(32, "lh t0, 0(a2)")));
  CALIB_BLOCK("sw_misaligned", 32, "addi a2, %[buf], 1", NORVC(REPT(32, "sw t0, 0(a2)")));
  CALIB_BLOCK("lw_misaligned_use", 32, "addi a2, %[buf], 1",
              NORVC(REPT(32, "lw t0, 0(a2)\n\taddi t1, t0, 1")));

  // Multiplier and divider.
  CALIB_BLOCK("mul", 32, "li t1, 3\n\tli t2, 5", NORVC(REPT(32, "mul t0, t1, t2")));
  CALIB_BLOCK("mul_use", 32, "li t1, 3\n\tli t2, 5",
              NORVC(REPT(32, "mul t0, t1, t2\n\taddi t3, t0, 1")));
  CALIB_BLOCK("mulh", 32, "li t1, 3\n\tli t2, 5", NORVC(REPT(32, "mulh t0, t1, t2")));
  CALIB_BLOCK("mulhu", 32, "li t1, 3\n\tli t2, 5", NORVC(REPT(32, "mulhu t0, t1, t2")));
  CALIB_BLOCK("mulhsu", 32, "li t1, 3\n\tli t2, 5", NORVC(REPT(32, "mulhsu t0, t1, t2")));
  CALIB_BLOCK("mulh_use", 32, "li t1, 3\n\tli t2, 5",
              NORVC(REPT(32, "mulh t0, t1, t2\n\taddi t3, t0, 1")));
  CALIB_BLOCK("div_by_1", 16, "li t1, 100\n\tli t2, 1", NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("div_by_7", 16, "li t1, 100\n\tli t2, 7", NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("div_by_0x10000", 16, "li t1, 100\n\tli t2, 0x10000",
              NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("div_by_0x40000000", 16, "li t1, 100\n\tli t2, 0x40000000",
              NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("div_by_0", 16, "li t1, 100\n\tli t2, 0", NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("div_by_m1", 16, "li t1, 100\n\tli t2, -1", NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("div_by_m7", 16, "li t1, 100\n\tli t2, -7", NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_BLOCK("divu_by_7", 16, "li t1, 100\n\tli t2, 7", NORVC(REPT(16, "divu t0, t1, t2")));
  CALIB_BLOCK("divu_by_m7", 16, "li t1, 100\n\tli t2, -7", NORVC(REPT(16, "divu t0, t1, t2")));
  CALIB_BLOCK("rem_by_7", 16, "li t1, 100\n\tli t2, 7", NORVC(REPT(16, "rem t0, t1, t2")));
  CALIB_BLOCK("remu_by_7", 16, "li t1, 100\n\tli t2, 7", NORVC(REPT(16, "remu t0, t1, t2")));
  CALIB_BLOCK("div_big_by_7", 16, "li t1, 0x7fffffff\n\tli t2, 7",
              NORVC(REPT(16, "div t0, t1, t2")));

  // CSR accesses.
  CALIB_BLOCK("csrr_mscratch", 32, "", NORVC(REPT(32, "csrr t0, 0x340")));
  CALIB_BLOCK("csrw_mscratch", 32, "", NORVC(REPT(32, "csrw 0x340, t0")));
  CALIB_BLOCK("csrr_mcycle", 32, "", NORVC(REPT(32, "csrr t0, 0xB00")));
  CALIB_BLOCK("csrr_mstatus", 32, "", NORVC(REPT(32, "csrr t0, 0x300")));
  CALIB_BLOCK("csrw_mstatus", 16, "csrr t1, 0x300", NORVC(REPT(16, "csrw 0x300, t1")));
  CALIB_BLOCK("csrw_mtvec", 16, "csrr t1, 0x305", NORVC(REPT(16, "csrw 0x305, t1")));
  CALIB_BLOCK("csrw_mie", 16, "csrr t1, 0x304", NORVC(REPT(16, "csrw 0x304, t1")));
  CALIB_BLOCK("csrr_misa", 32, "", NORVC(REPT(32, "csrr t0, 0x301")));
  CALIB_BLOCK("csrw_mhpmevent3", 16, "csrr t1, 0x323", NORVC(REPT(16, "csrw 0x323, t1")));
  CALIB_BLOCK("csrr_use", 32, "", NORVC(REPT(32, "csrr t0, 0x340\n\taddi t1, t0, 1")));

  // Fences.
  CALIB_BLOCK("fence", 16, "", NORVC(REPT(16, "fence")));
  CALIB_BLOCK("fence_i", 16, "", NORVC(REPT(16, "fence.i")));

  // Stall events of the RTL (mhpmevent3 bits 2, 3, 4).
  CALIB_HPM("lw_use", HPM_LD_STALL, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\taddi t1, t0, 1")));
  CALIB_HPM("lw_nouse", HPM_LD_STALL, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\taddi t1, t2, 1")));
  CALIB_HPM("jalr_dep", HPM_JR_STALL, "", NORVC(REPT(32, JALR_DEP)));
  CALIB_HPM("jalr_nodep", HPM_JR_STALL, "", NORVC(REPT(32, JALR_NODEP)));
  CALIB_HPM("branch_taken", HPM_IMISS, "", NORVC(REPT(64, "beq x0, x0, 1f\n1:")));
  CALIB_HPM("branch_taken_misaligned", HPM_IMISS, "", RVC("c.nop") "\n\t"
                                                      NORVC(REPT(64, "beq x0, x0, 1f\n1:")));
  CALIB_HPM("jal", HPM_IMISS, "", NORVC(REPT(64, "jal x0, 1f\n1:")));
  CALIB_HPM("nop", HPM_IMISS, "", NORVC(REPT(64, "nop")));

  return 0;
}
