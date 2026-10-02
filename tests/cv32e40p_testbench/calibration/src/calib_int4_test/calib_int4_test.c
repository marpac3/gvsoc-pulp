// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// RV32IMC timing: the trap entry of the other illegal instructions, the fetch after a return or a
// fence, the JALR hazards on the other producers. Blocks are 32-bit unless named c_* or *_odd*.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"
#define RVC(s)   ".option push\n\t.option rvc\n\t" s "\n\t.option pop"

// Trap handler at a 256-byte aligned address (mtvec base), skipping
// the 4-byte trapping instruction. It is 32-bit code, so that the
// handler timing is the one of calib_int2_test with norvc. mtvec is
// saved in a5.
#define TRAP_PRE  "j 2f\n\t.p2align 8\n3:\n\t" \
                  NORVC("csrr a3, 0x341\n\taddi a3, a3, 4\n\tcsrw 0x341, a3\n\tmret") \
                  "\n2:\n\tcsrr a5, 0x305\n\tla a4, 3b\n\tcsrw 0x305, a4"
#define TRAP_POST "\n\tcsrw 0x305, a5"

// The address of the next group (label 1) stored at calib_buf + off, for a jalr through a load.
#define LA_SW(off)  "la t2, 1f\n\tsw t2, " #off "(%[buf])\n\t"

int main(void)
{
  calib_init();

  // Trap entry: the handler alone (ecall), a 32-bit illegal opcode (OP-V), an illegal CSR address,
  // a 16-bit illegal instruction followed by a c.nop (the handler skips both).
  CALIB_BLOCK_POST("ecall_mret_norvc", 16, TRAP_PRE, NORVC(REPT(16, "ecall")), TRAP_POST);
  CALIB_BLOCK_POST("illegal32_mret", 16, TRAP_PRE, NORVC(REPT(16, ".word 0x00000057")), TRAP_POST);
  CALIB_BLOCK_POST("illegal_csr_mret", 16, TRAP_PRE, NORVC(REPT(16, "csrr t1, 0x5C0")), TRAP_POST);
  CALIB_BLOCK_POST("illegal16_mret", 16, TRAP_PRE, REPT(16, ".hword 0x0000\n\t" RVC("c.nop")),
                   TRAP_POST);

  // Return to a 32-bit instruction at a halfword address, and at a word address.
  CALIB_BLOCK_POST("mret_odd32", 16, "csrr a5, 0x300",
                   REPT(16, NORVC("la t0, 1f\n\tcsrw 0x341, t0\n\tmret") "\n\t" RVC("c.nop")
                            "\n1:\n\t" NORVC("addi t1, t1, 0") "\n\t" RVC("c.nop")),
                   "\n\tcsrw 0x300, a5");
  CALIB_BLOCK_POST("mret_even32", 16, "csrr a5, 0x300",
                   NORVC(REPT(16, "la t0, 1f\n\tcsrw 0x341, t0\n\tmret\n1:\n\taddi t1, t1, 0")),
                   "\n\tcsrw 0x300, a5");

  // After fence and fence.i: a 32-bit instruction at a halfword address, then at a word address.
  CALIB_BLOCK("fencei_odd32", 16, "",
              REPT(16, RVC("c.nop") "\n\t" NORVC("fence.i\n\taddi t1, t1, 0") "\n\t" RVC("c.nop")));
  CALIB_BLOCK("fencei_even32", 16, "", NORVC(REPT(16, "fence.i\n\taddi t1, t1, 0")));
  CALIB_BLOCK("fence_odd32", 16, "",
              REPT(16, RVC("c.nop") "\n\t" NORVC("fence\n\taddi t1, t1, 0") "\n\t" RVC("c.nop")));
  CALIB_BLOCK("fence_even32", 16, "", NORVC(REPT(16, "fence\n\taddi t1, t1, 0")));

  // jalr whose rs1 comes from a CSR read: a status CSR (mepc, flush behind it) and mscratch.
  CALIB_BLOCK("csrr_mepc_jr", 16, "",
              NORVC(REPT(16,
                         "la t0, 1f\n\tcsrw 0x341, t0\n\tcsrr t0, 0x341\n\tjalr x0, 0(t0)\n1:")));
  CALIB_BLOCK("csrr_mscratch_jr", 16, "",
              NORVC(REPT(16,
                         "la t0, 1f\n\tcsrw 0x340, t0\n\tcsrr t0, 0x340\n\tjalr x0, 0(t0)\n1:")));

  // jalr whose rs1 comes from the divider and from the multi-cycle multiplier.
  CALIB_BLOCK("div_jr", 16, "li t4, 1",
              NORVC(REPT(16, "la t3, 1f\n\tdiv t0, t3, t4\n\tjalr x0, 0(t0)\n1:")));
  CALIB_BLOCK("mulhu_jr", 16, "li t4, 0x80000000",
              NORVC(REPT(16,
                         "la t3, 1f\n\tslli t3, t3, 1\n\tmulhu t0, t3, t4\n\tjalr x0, 0(t0)\n1:")));

  // Misaligned loads: the next instruction uses the value, a jalr at distance 0 and 1 (the target
  // is stored misaligned too).
  CALIB_BLOCK("lw_mis_use", 32, "", NORVC(REPT(32, "lw t0, 2(%[buf])\n\taddi t1, t0, 1")));
  CALIB_BLOCK("jalr_lw_mis0", 16, "",
              NORVC(REPT(16, LA_SW(6) "lw t0, 6(%[buf])\n\tjalr x0, 0(t0)\n1:")));
  CALIB_BLOCK("jalr_lw_mis1", 16, "",
              NORVC(REPT(16, LA_SW(6) "lw t0, 6(%[buf])\n\tnop\n\tjalr x0, 0(t0)\n1:")));

  // A load two instructions before a jalr, with a multi-cycle mulhu or a status CSR read between.
  CALIB_BLOCK("jalr_lw_mulhu", 16, "li t4, 3",
              NORVC(REPT(16, LA_SW(0)
                             "lw t0, 0(%[buf])\n\tmulhu t1, t4, t4\n\tjalr x0, 0(t0)\n1:")));
  CALIB_BLOCK("jalr_lw_csrr_status", 16, "",
              NORVC(REPT(16, LA_SW(0) "lw t0, 0(%[buf])\n\tcsrr t1, 0x300\n\tjalr x0, 0(t0)\n1:")));

  // Compressed load then c.jr, at distance 0 and 1 (a2 = calib_buf).
  CALIB_BLOCK("c_lw_c_jr0", 16, "mv a2, %[buf]",
              REPT(16, NORVC(LA_SW(0) "nop") "\n\t" RVC("c.lw a3, 0(a2)\n\tc.jr a3") "\n1:"));
  CALIB_BLOCK("c_lw_c_jr1", 16, "mv a2, %[buf]",
              REPT(16, NORVC(LA_SW(0)) "\n\t" RVC("c.lw a3, 0(a2)\n\tc.nop\n\tc.jr a3\n\tc.nop")
                       "\n1:"));

  // Event lines of the stalls above (jr_stall 0x08, ld_stall 0x04).
  CALIB_HPM("jalr_lw0", 0x08, "",
            NORVC(REPT(16, LA_SW(0) "lw t0, 0(%[buf])\n\tjalr x0, 0(t0)\n1:")));
  CALIB_HPM("jalr_lw0", 0x04, "",
            NORVC(REPT(16, LA_SW(0) "lw t0, 0(%[buf])\n\tjalr x0, 0(t0)\n1:")));
  CALIB_HPM("jalr_lw1", 0x04, "",
            NORVC(REPT(16, LA_SW(0) "lw t0, 0(%[buf])\n\tnop\n\tjalr x0, 0(t0)\n1:")));
  CALIB_HPM("csrr_mscratch_jr", 0x08, "",
            NORVC(REPT(16, "la t0, 1f\n\tcsrw 0x340, t0\n\tcsrr t0, 0x340\n\tjalr x0, 0(t0)\n1:")));
  CALIB_HPM("csrr_mepc_jr", 0x08, "",
            NORVC(REPT(16, "la t0, 1f\n\tcsrw 0x341, t0\n\tcsrr t0, 0x341\n\tjalr x0, 0(t0)\n1:")));
  CALIB_HPM("div_jr", 0x08, "li t4, 1",
            NORVC(REPT(16, "la t3, 1f\n\tdiv t0, t3, t4\n\tjalr x0, 0(t0)\n1:")));
  CALIB_HPM("c_lw_c_jr1", 0x08, "mv a2, %[buf]",
            REPT(16, NORVC(LA_SW(0)) "\n\t" RVC("c.lw a3, 0(a2)\n\tc.nop\n\tc.jr a3\n\tc.nop")
                     "\n1:"));

  return 0;
}
