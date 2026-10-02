// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// RV32IMC timing: the bubble behind a JALR, which keeps the JALR encoding for the jump register
// hazard check, and the load stall on an ALU write of the destination of the load in EX.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"
#define RVC(s)   ".option push\n\t.option rvc\n\t" s "\n\t.option pop"

int main(void)
{
  calib_init();

  // A JALR writing its own target register, or another one. The nop keeps the address out of EX.
  CALIB_BLOCK("jalr_rd_rs1", 16, "", NORVC(REPT(16, "la t0, 1f\n\tnop\n\tjalr t0, 0(t0)\n1:")));
  CALIB_BLOCK("jalr_rd_other", 16, "", NORVC(REPT(16, "la t0, 1f\n\tnop\n\tjalr t1, 0(t0)\n1:")));
  CALIB_BLOCK("jalr_x0", 16, "", NORVC(REPT(16, "la t0, 1f\n\tnop\n\tjalr x0, 0(t0)\n1:")));
  // The same right behind the address (a jump register stall first).
  CALIB_BLOCK("la_jalr_rd_rs1", 16, "", NORVC(REPT(16, "la t0, 1f\n\tjalr t0, 0(t0)\n1:")));
  CALIB_BLOCK("la_jalr_rd_other", 16, "", NORVC(REPT(16, "la t0, 1f\n\tjalr t1, 0(t0)\n1:")));
  // Compressed: c.jalr ra writes ra, c.jalr t0 and c.jr t0 do not write t0 (16 bytes an iteration).
  CALIB_BLOCK("c_jalr_ra", 16, "",
              REPT(16, NORVC("la ra, 1f\n\tnop") "\n\t" RVC("c.jalr ra\n\tc.nop") "\n1:"));
  CALIB_BLOCK("c_jalr_t0", 16, "",
              REPT(16, NORVC("la t0, 1f\n\tnop") "\n\t" RVC("c.jalr t0\n\tc.nop") "\n1:"));
  CALIB_BLOCK("c_jr_t0", 16, "",
              REPT(16, NORVC("la t0, 1f\n\tnop") "\n\t" RVC("c.jr t0\n\tc.nop") "\n1:"));

  // A load, then an ALU write of its destination: a CSR read, a multiplication, lui, x0 (nop behind
  // a load writing x0), a compressed load and c.li, an ALU write of another register.
  CALIB_BLOCK("lw_csrr_waw", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tcsrr t0, 0x340")));
  CALIB_BLOCK("lw_mul_waw", 32, "li t1, 3", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tmul t0, t1, t1")));
  CALIB_BLOCK("lw_lui_waw", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tlui t0, 1")));
  CALIB_BLOCK("lw_x0_nop", 32, "", NORVC(REPT(32, "lw x0, 0(%[buf])\n\tnop")));
  CALIB_BLOCK("lw_x0_addi", 32, "", NORVC(REPT(32, "lw x0, 0(%[buf])\n\taddi t1, t1, 1")));
  CALIB_BLOCK("c_lw_c_li_waw", 32, "mv a2, %[buf]", RVC(REPT(32, "c.lw a3, 0(a2)\n\tc.li a3, 1")));
  CALIB_BLOCK("lw_li_other", 32, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tli t1, 1")));
  // A jump writing the register of the load before it: it sets the PC during the load stall.
  CALIB_BLOCK("lw_jal_waw", 16, "", NORVC(REPT(16, "lw t0, 0(%[buf])\n\tjal t0, 1f\n1:")));
  CALIB_BLOCK("lw_jal_other", 16, "", NORVC(REPT(16, "lw t1, 0(%[buf])\n\tjal t0, 1f\n1:")));

  // Event lines: the jump register stall of the bubble, the load stall of the ALU write.
  CALIB_HPM("jalr_rd_rs1", 0x08, "", NORVC(REPT(16, "la t0, 1f\n\tnop\n\tjalr t0, 0(t0)\n1:")));
  CALIB_HPM("lw_li_waw", 0x04, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\tli t0, 1")));
  CALIB_HPM("lw_x0_nop", 0x04, "", NORVC(REPT(32, "lw x0, 0(%[buf])\n\tnop")));

  return 0;
}
