// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// RV32IMC timing: a jump in ID behind a multi-cycle instruction in EX, which sets the PC once its
// target is known (jump_in_dec), and a load writing the register of the load in EX (load_stall_o).

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"
#define RVC(s)   ".option push\n\t.option rvc\n\t" s "\n\t.option pop"

#define DIV_PRE "li t1, 100\n\tli t2, 7"

int main(void)
{
  calib_init();

  // Jumps behind the divider (clz(7) + 3 = 32 cycles), the multiplier, a misaligned load.
  CALIB_BLOCK("div_jal", 16, DIV_PRE, NORVC(REPT(16, "div t0, t1, t2\n\tj 1f\n1:")));
  CALIB_BLOCK("div_c_j", 16, DIV_PRE,
              REPT(16, NORVC("div t0, t1, t2") "\n\t" RVC("c.j 1f\n\tc.nop") "\n1:"));
  CALIB_BLOCK("div_jal_link", 16, DIV_PRE, NORVC(REPT(16, "div t0, t1, t2\n\tjal t3, 1f\n1:")));
  CALIB_BLOCK("mulhu_jal", 16, "li t1, 3\n\tli t2, 5",
              NORVC(REPT(16, "mulhu t0, t1, t2\n\tj 1f\n1:")));
  CALIB_BLOCK("lw_mis_jal", 16, "", NORVC(REPT(16, "lw t0, 2(%[buf])\n\tj 1f\n1:")));
  CALIB_BLOCK("mul_jal", 16, "li t1, 3\n\tli t2, 5", NORVC(REPT(16, "mul t0, t1, t2\n\tj 1f\n1:")));

  // The target is a 32-bit instruction at a halfword address (16 bytes an iteration, the c.nop
  // after it executes): two fetches, hidden too if the jump waits long enough.
  CALIB_BLOCK("div_jal_odd32", 16, DIV_PRE,
              REPT(16, NORVC("div t0, t1, t2\n\tj 1f") "\n\t" RVC("c.nop") "\n1:\n\t"
                       NORVC("addi t3, t3, 1") "\n\t" RVC("c.nop")));
  CALIB_BLOCK("lw_mis_jal_odd32", 16, "",
              REPT(16, NORVC("lw t0, 2(%[buf])\n\tj 1f") "\n\t" RVC("c.nop") "\n1:\n\t"
                       NORVC("addi t3, t3, 1") "\n\t" RVC("c.nop")));
  CALIB_BLOCK("nop_jal_odd32", 16, "",
              REPT(16, NORVC("nop\n\tj 1f") "\n\t" RVC("c.nop") "\n1:\n\t" NORVC("addi t3, t3, 1")
                       "\n\t" RVC("c.nop")));

  // JALR behind the divider: on an older register, on the divider result (t0 = t3 / 1).
  CALIB_BLOCK("div_jalr", 16, DIV_PRE,
              NORVC(REPT(16, "la t3, 1f\n\tdiv t0, t1, t2\n\tjalr x0, 0(t3)\n1:")));
  CALIB_BLOCK("div_jalr_dep", 16, "li t4, 1",
              NORVC(REPT(16, "la t3, 1f\n\tdiv t0, t3, t4\n\tjalr x0, 0(t0)\n1:")));
  // JALR on a load two instructions before, behind the multiplier (the load leaves WB meanwhile).
  CALIB_BLOCK("lw_mulhu_jalr", 16, "li t4, 3",
              NORVC(REPT(16,
                         "la t2, 1f\n\tsw t2, 0(%[buf])\n\tlw t0, 0(%[buf])\n\tmulhu t1, t4, t4\n\t"
                         "jalr x0, 0(t0)\n1:")));

  // A load writing the register of the load in EX, an ALU op doing so, a load reading it.
  CALIB_BLOCK("lw_lw_waw", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tlw t0, 4(%[buf])")));
  CALIB_BLOCK("lw_li_waw", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tli t0, 1")));
  CALIB_BLOCK("lw_lw_other", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tlw t1, 4(%[buf])")));
  CALIB_BLOCK("c_lw_c_lw_waw", 32, "mv a2, %[buf]",
              RVC(REPT(32, "c.lw a3, 0(a2)\n\tc.lw a3, 4(a2)")));

  // Event lines: jumps and load-use stalls of the blocks above.
  CALIB_HPM("div_jal", 0x80, DIV_PRE, NORVC(REPT(16, "div t0, t1, t2\n\tj 1f\n1:")));
  CALIB_HPM("lw_lw_waw", 0x04, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tlw t0, 4(%[buf])")));

  return 0;
}
