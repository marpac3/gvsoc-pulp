// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// FPU timing: the load stall when an FP operation in ID writes the destination of the load in EX
// (load_stall_o), after an FP load and after an integer load.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#ifdef __riscv_zfinx
#define FA "t3"
#define FB "t4"
#define FC "t5"
#define FD "t6"
#define LDA "lw t3, 0(%[buf])"
#define FP_PRE "li t4, 0x3f800000\n\tli t5, 0x40400000\n\tli t6, 0x40000000"
#else
#define FA "ft0"
#define FB "ft1"
#define FC "ft2"
#define FD "ft3"
#define LDA "flw ft0, 0(%[buf])"
#define FP_PRE "li t1, 0x3f800000\n\tfmv.w.x ft1, t1\n\tli t1, 0x40400000\n\tfmv.w.x ft2, t1\n\t" \
               "li t1, 0x40000000\n\tfmv.w.x ft3, t1"
#endif

int main(void)
{
  calib_init();
#ifndef __riscv_zfinx
  // mstatus.FS = Initial: the F instructions are legal.
  __asm__ volatile("li t0, 0x2000\n\tcsrs 0x300, t0" ::: "t0");
#endif

  // A load of FA (flw, lw with Zfinx), then an FP operation writing FA or another register.
  CALIB_BLOCK("ld_fadd_waw", 32, FP_PRE, NORVC(REPT(32, LDA "\n\tfadd.s " FA ", " FB ", " FC)));
  CALIB_BLOCK("ld_fadd_other", 32, FP_PRE, NORVC(REPT(32, LDA "\n\tfadd.s " FD ", " FB ", " FC)));
  CALIB_BLOCK("ld_fmin_waw", 32, FP_PRE, NORVC(REPT(32, LDA "\n\tfmin.s " FA ", " FB ", " FC)));
  CALIB_BLOCK("ld_fsgnj_waw", 32, FP_PRE, NORVC(REPT(32, LDA "\n\tfsgnj.s " FA ", " FB ", " FC)));
  // An integer load, then an FP operation writing its destination: compare, conversion.
  CALIB_BLOCK("lw_feq_waw", 32, FP_PRE,
              NORVC(REPT(32, "lw t0, 0(%[buf])\n\tfeq.s t0, " FB ", " FC)));
  CALIB_BLOCK("lw_fcvt_w_waw", 32, FP_PRE, NORVC(REPT(32, "lw t0, 0(%[buf])\n\tfcvt.w.s t0, " FB)));
  CALIB_BLOCK("lw_feq_other", 32, FP_PRE,
              NORVC(REPT(32, "lw t0, 0(%[buf])\n\tfeq.s t1, " FB ", " FC)));
#ifndef __riscv_zfinx
  CALIB_BLOCK("flw_fmv_w_x_waw", 32, "li t1, 1", NORVC(REPT(32, LDA "\n\tfmv.w.x " FA ", t1")));
#endif

  CALIB_HPM("ld_fadd_waw", 0x04, FP_PRE, NORVC(REPT(32, LDA "\n\tfadd.s " FA ", " FB ", " FC)));

  return 0;
}
