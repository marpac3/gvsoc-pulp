// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// FPU timing: the F instructions, their result used by the next instruction, the FP loads and
// stores, the FP CSRs. 32-bit blocks. With Zfinx the FP loads, stores and moves are left out.

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

#define OP3(op)  op " " FA ", " FB ", " FC
#define OP4(op)  op " " FA ", " FB ", " FC ", " FD
#define DEP3(op) op " " FA ", " FB ", " FC "\n\t" op " " FB ", " FA ", " FC

int main(void)
{
  calib_init();
#ifndef __riscv_zfinx
  // mstatus.FS = Initial: the F instructions are legal.
  __asm__ volatile("li t0, 0x2000\n\tcsrs 0x300, t0" ::: "t0");
#endif

  // Arithmetic, independent and with the result used by the next instruction.
  CALIB_BLOCK("fadd", 32, FP_PRE, NORVC(REPT(32, OP3("fadd.s"))));
  CALIB_BLOCK("fadd_dep", 16, FP_PRE, NORVC(REPT(16, DEP3("fadd.s"))));
  CALIB_BLOCK("fsub", 32, FP_PRE, NORVC(REPT(32, OP3("fsub.s"))));
  CALIB_BLOCK("fmul", 32, FP_PRE, NORVC(REPT(32, OP3("fmul.s"))));
  CALIB_BLOCK("fmul_dep", 16, FP_PRE, NORVC(REPT(16, DEP3("fmul.s"))));
  CALIB_BLOCK("fmadd", 32, FP_PRE, NORVC(REPT(32, OP4("fmadd.s"))));
  CALIB_BLOCK("fmadd_dep", 16, FP_PRE,
              NORVC(REPT(16, OP4("fmadd.s") "\n\tfmadd.s " FB ", " FA ", " FC ", " FD)));
  CALIB_BLOCK("fdiv", 16, FP_PRE, NORVC(REPT(16, OP3("fdiv.s"))));
  CALIB_BLOCK("fdiv_dep", 8, FP_PRE, NORVC(REPT(8, DEP3("fdiv.s"))));
  CALIB_BLOCK("fsqrt", 16, FP_PRE, NORVC(REPT(16, "fsqrt.s " FA ", " FC)));
  CALIB_BLOCK("fdiv_fadd", 16, FP_PRE,
              NORVC(REPT(16, OP3("fdiv.s") "\n\tfadd.s " FD ", " FB ", " FC)));
  CALIB_BLOCK("fdiv_int", 16, FP_PRE, NORVC(REPT(16, OP3("fdiv.s") "\n\taddi t1, t1, 1")));

  // Non-computational and conversions.
  CALIB_BLOCK("fmin", 32, FP_PRE, NORVC(REPT(32, OP3("fmin.s"))));
  CALIB_BLOCK("fsgnj", 32, FP_PRE, NORVC(REPT(32, OP3("fsgnj.s"))));
  CALIB_BLOCK("feq", 32, FP_PRE, NORVC(REPT(32, "feq.s t1, " FB ", " FC)));
  CALIB_BLOCK("feq_use", 32, FP_PRE, NORVC(REPT(32, "feq.s t1, " FB ", " FC "\n\taddi t2, t1, 1")));
  CALIB_BLOCK("fclass", 32, FP_PRE, NORVC(REPT(32, "fclass.s t1, " FB)));
  CALIB_BLOCK("fcvt_w_s", 32, FP_PRE, NORVC(REPT(32, "fcvt.w.s t1, " FB)));
  CALIB_BLOCK("fcvt_s_w", 32, FP_PRE "\n\tli t1, 7", NORVC(REPT(32, "fcvt.s.w " FA ", t1")));
  CALIB_BLOCK("fadd_feq", 16, FP_PRE, NORVC(REPT(16, OP3("fadd.s") "\n\tfeq.s t1, " FA ", " FC)));
#ifndef __riscv_zfinx
  CALIB_BLOCK("fmv_x_w", 32, FP_PRE, NORVC(REPT(32, "fmv.x.w t1, " FB)));
  CALIB_BLOCK("fmv_w_x", 32, FP_PRE, NORVC(REPT(32, "fmv.w.x " FA ", t1")));
  CALIB_BLOCK("flw", 32, FP_PRE, NORVC(REPT(32, "flw " FA ", 0(%[buf])")));
  CALIB_BLOCK("flw_use", 32, FP_PRE,
              NORVC(REPT(32, "flw " FA ", 0(%[buf])\n\tfadd.s " FB ", " FA ", " FC)));
  CALIB_BLOCK("fsw", 32, FP_PRE, NORVC(REPT(32, "fsw " FB ", 4(%[buf])")));
  CALIB_BLOCK("fadd_fsw", 16, FP_PRE, NORVC(REPT(16, OP3("fadd.s") "\n\tfsw " FA ", 4(%[buf])")));
#else
  CALIB_BLOCK("lw_fadd_use", 32, FP_PRE,
              NORVC(REPT(32, "lw " FA ", 0(%[buf])\n\tfadd.s " FB ", " FA ", " FC)));
  CALIB_BLOCK("fadd_sw", 16, FP_PRE, NORVC(REPT(16, OP3("fadd.s") "\n\tsw " FA ", 4(%[buf])")));
#endif

  // FP CSRs: frm and fcsr writes are status CSR accesses in the decoder, fflags is not.
  CALIB_BLOCK("csrw_frm", 16, "", NORVC(REPT(16, "csrw 0x002, x0")));
  CALIB_BLOCK("csrw_fflags", 16, "", NORVC(REPT(16, "csrw 0x001, x0")));
  CALIB_BLOCK("csrr_fcsr", 16, "", NORVC(REPT(16, "csrr t1, 0x003")));
  CALIB_BLOCK("fadd_csrr_fflags", 16, FP_PRE, NORVC(REPT(16, OP3("fadd.s") "\n\tcsrr t1, 0x001")));

  return 0;
}
