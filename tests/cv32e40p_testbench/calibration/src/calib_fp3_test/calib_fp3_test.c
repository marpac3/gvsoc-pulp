// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// FPU timing: the divider result kept in EX (APU_Result_Memorization), a jump behind an FP
// operation waiting in EX, an FP result written to x0, the APU event lines of the HPM counters.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#ifdef __riscv_zfinx
#define FA "t3"
#define FB "t4"
#define FC "t5"
#define FD "t6"
#define FP_PRE "li t4, 0x3f800000\n\tli t5, 0x40400000\n\tli t6, 0x40000000"
#else
#define FA "ft0"
#define FB "ft1"
#define FC "ft2"
#define FD "ft3"
#define FP_PRE "li t1, 0x3f800000\n\tfmv.w.x ft1, t1\n\tli t1, 0x40400000\n\tfmv.w.x ft2, t1\n\t" \
               "li t1, 0x40000000\n\tfmv.w.x ft3, t1"
#endif

#define ADD(d, a, b) "fadd.s " d ", " a ", " b
#define DIV(d, a, b) "fdiv.s " d ", " a ", " b
#define ADDI(n) REPT(n, "addi t1, t1, 1")
// a2 = calib_buf, advanced by the post-increment loads.
#define BUF_PRE FP_PRE "\n\tmv a2, %[buf]"

int main(void)
{
  calib_init();
#ifndef __riscv_zfinx
  // mstatus.FS = Initial: the F instructions are legal.
  __asm__ volatile("li t0, 0x2000\n\tcsrs 0x300, t0" ::: "t0");
#endif

  // The result due while the instruction n after the fdiv is in EX (n
  // = 18), that instruction being a misaligned load, a post-increment
  // load, a mulhu, an addi behind a load; the addi after it writes.
  CALIB_BLOCK("fdiv_mis_lw", 4, BUF_PRE,
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(17) "\n\tlw t2, 2(%[buf])\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_cv_lw", 4, BUF_PRE,
              NORVC(REPT(4, "mv a2, %[buf]\n\t" DIV(FA, FB, FC) "\n\t" ADDI(17)
                            "\n\tcv.lw t2, (a2), 4\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_cv_lw3", 4, BUF_PRE,
              NORVC(REPT(4, "mv a2, %[buf]\n\t" DIV(FA, FB, FC) "\n\t" ADDI(17) "\n\t"
                            REPT(3, "cv.lw t2, (a2), 4") "\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_cv_sw", 4, BUF_PRE,
              NORVC(REPT(4, "mv a2, %[buf]\n\t" DIV(FA, FB, FC) "\n\t" ADDI(17)
                            "\n\tcv.sw t2, (a2), 4\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_mulhu", 4, BUF_PRE "\n\tli t2, 3",
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(16) "\n\tmulhu t0, t2, t2\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_lw_addi", 4, BUF_PRE,
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(16) "\n\tlw t2, 0(%[buf])\n\t" ADDI(5))));
  CALIB_BLOCK("fdiv_sw_addi", 4, BUF_PRE,
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(16) "\n\tsw t2, 0(%[buf])\n\t" ADDI(5))));
  CALIB_BLOCK("fdiv_div", 4, BUF_PRE "\n\tli t2, 7",
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(16) "\n\tdiv t0, t1, t2\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_div2", 4, BUF_PRE "\n\tli t2, 7",
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(10) "\n\tdiv t0, t1, t2\n\t" ADDI(4))));
  // A JALR in ID as the result is due (17 or 18 addi before it), on a register written long before.
  CALIB_BLOCK("fdiv_jalr17", 4, BUF_PRE,
              NORVC(REPT(4, "la a3, 1f\n\t" DIV(FA, FB, FC) "\n\t" ADDI(17)
                            "\n\tjalr x0, 0(a3)\n1:\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_jalr18", 4, BUF_PRE,
              NORVC(REPT(4, "la a3, 1f\n\t" DIV(FA, FB, FC) "\n\t" ADDI(18)
                            "\n\tjalr x0, 0(a3)\n1:\n\t" ADDI(4))));
  CALIB_BLOCK("fdiv_jal17", 4, BUF_PRE,
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(17) "\n\tj 1f\n1:\n\t" ADDI(4))));

  // A jump in ID while the FP operation before it waits for the divider in EX.
  CALIB_BLOCK("fdiv_fadd_jal", 8, FP_PRE,
              NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FB, FC) "\n\tj 1f\n1:")));
  CALIB_BLOCK("fdiv_fadd_addi", 8, FP_PRE,
              NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FB, FC) "\n\taddi t1, t1, 1")));

  // An FP result written to x0: the instructions reading x0 through an operand mux wait for it
  // (operand c is x0 by default, a branch reads rs2), a store does not (operand c is rs2), nor a
  // branch on other registers.
  CALIB_BLOCK("feq_x0_addi", 32, FP_PRE,
              NORVC(REPT(32, "feq.s x0, " FB ", " FC "\n\taddi t1, t1, 1")));
  CALIB_BLOCK("feq_x0_sw", 32, FP_PRE,
              NORVC(REPT(32, "feq.s x0, " FB ", " FC "\n\tsw t1, 0(%[buf])")));
  CALIB_BLOCK("feq_x0_beq_x0", 32, FP_PRE "\n\tli t1, 1",
              NORVC(REPT(32, "feq.s x0, " FB ", " FC "\n\tbeq t1, x0, 1f\n1:")));
  CALIB_BLOCK("feq_x0_beq", 32, FP_PRE "\n\tli t1, 1\n\tli t2, 2",
              NORVC(REPT(32, "feq.s x0, " FB ", " FC "\n\tbeq t1, t2, 1f\n1:")));
  CALIB_BLOCK("fcvt_x0_lw", 32, FP_PRE, NORVC(REPT(32, "fcvt.w.s x0, " FB "\n\tlw t1, 0(%[buf])")));

  // APU events: type conflicts 0x1000, contention 0x2000, dependencies 0x4000, write-back 0x8000.
  CALIB_HPM("fdiv_fadd_type", 0x1000, FP_PRE,
            NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FB, FC))));
  CALIB_HPM("fdiv_fadd_cont", 0x2000, FP_PRE,
            NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FB, FC))));
  CALIB_HPM("fdiv_use_dep", 0x4000, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FA, FC))));
  CALIB_HPM("fadd_dep_dep", 0x4000, FP_PRE,
            NORVC(REPT(16, ADD(FA, FB, FC) "\n\t" ADD(FB, FA, FC))));
  CALIB_HPM("fdiv_fadd_use_type", 0x1000, FP_PRE,
            NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FB, FC) "\n\t" ADD(FB, FA, FC))));
  CALIB_HPM("fdiv_fadd_use_dep", 0x4000, FP_PRE,
            NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FB, FC) "\n\t" ADD(FB, FA, FC))));
  CALIB_HPM("fdiv_addi20_wb", 0x8000, FP_PRE, NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI(20))));
  CALIB_HPM("fdiv_csrr_dep", 0x4000, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\tcsrr t1, 0x340")));
  CALIB_HPM("fadd_addi_wb", 0x8000, FP_PRE, NORVC(REPT(32, ADD(FA, FB, FC) "\n\taddi t1, t1, 1")));
  CALIB_HPM("fadd_dep_all", 0xF000, FP_PRE,
            NORVC(REPT(16, ADD(FA, FB, FC) "\n\t" ADD(FB, FA, FC))));

  return 0;
}
