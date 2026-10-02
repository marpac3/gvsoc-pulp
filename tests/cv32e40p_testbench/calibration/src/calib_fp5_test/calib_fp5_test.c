// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// FPU timing: the multi-cycle results kept in EX behind a misaligned access, a post-increment load
// or a mulh, and the dispatcher on the rs2 field of the fences and ebreak (default operand muxes).

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#ifdef __riscv_zfinx
#define FA "t3"
#define FB "t4"
#define FC "t5"
#define FP_PRE "li t3, 0xc0800000\n\tli t4, 0x3f800000\n\tli t5, 0x40400000"
#else
#define FA "ft0"
#define FB "ft1"
#define FC "ft2"
#define FP_PRE "li t1, 0xc0800000\n\tfmv.w.x ft0, t1\n\tli t1, 0x3f800000\n\tfmv.w.x ft1, t1\n\t" \
               "li t1, 0x40400000\n\tfmv.w.x ft2, t1"
#endif
// fsqrt of a negative number (FA = -4.0): two cycles in the divider (ex1_srt_skip).
#define SQRT_NEG "fsqrt.s " FB ", " FA
#define MUL_PRE  FP_PRE "\n\tli t1, 3\n\tli t2, 5"

// Trap handler at a 256-byte aligned address (mtvec base), skipping the 4-byte trapping
// instruction, as in calib_int4_test. mtvec is saved in a5 and restored by TRAP_POST.
#define TRAP_PRE  "j 2f\n\t.p2align 8\n3:\n\t" \
                  NORVC("csrr a3, 0x341\n\taddi a3, a3, 4\n\tcsrw 0x341, a3\n\tmret") \
                  "\n2:\n\tcsrr a5, 0x305\n\tla a4, 3b\n\tcsrw 0x305, a4"
#define TRAP_POST "\n\tcsrw 0x305, a5"

int main(void)
{
  calib_init();
#ifndef __riscv_zfinx
  // mstatus.FS = Initial: the F instructions are legal.
  __asm__ volatile("li t0, 0x2000\n\tcsrs 0x300, t0" ::: "t0");
#endif

  // The fsqrt result returns in the second mulh cycle (held to its end) or in the first (written).
  CALIB_BLOCK("sqrt_mulh_csr", 8, MUL_PRE,
              NORVC(REPT(8, SQRT_NEG "\n\tmulh t0, t1, t2\n\tcsrrw a4, 0x340, a4")));
  CALIB_BLOCK("sqrt_nop_mulh_csr", 8, MUL_PRE,
              NORVC(REPT(8, SQRT_NEG "\n\tnop\n\tmulh t0, t1, t2\n\tcsrrw a4, 0x340, a4")));
  CALIB_BLOCK("sqrt_mulh_use", 8, MUL_PRE,
              NORVC(REPT(8, SQRT_NEG "\n\tmulh t0, t1, t2\n\tfadd.s " FC ", " FB ", " FB)));
  CALIB_BLOCK("sqrt_mulh_fadd", 8, MUL_PRE,
              NORVC(REPT(8, SQRT_NEG "\n\tmulh t0, t1, t2\n\tfadd.s " FC ", " FA ", " FA)));
  // A misaligned store (two transfers) and a post-increment load in EX when the result returns.
  CALIB_BLOCK("sqrt_sw_mis_csr", 8, MUL_PRE,
              NORVC(REPT(8, SQRT_NEG "\n\tsw t0, 1(%[buf])\n\tcsrr a4, 0x340")));
  CALIB_BLOCK("sqrt_lw_pi_csr", 8, MUL_PRE "\n\tmv a2, %[buf]",
              NORVC(REPT(8, SQRT_NEG "\n\tcv.lw t0, (a2), 4\n\tcsrr a4, 0x340")));
  // A latency-2 result (multi-cycle with FPU_ADDMUL_LAT = 2) in the first or the second cycle of
  // the mulh, then another FP operation.
  CALIB_BLOCK("fadd_mulh_fadd", 8, MUL_PRE,
              NORVC(REPT(8, "fadd.s " FC ", " FB ", " FB "\n\tmulh t0, t1, t2\n\tfadd.s " FC ", " FB
                            ", " FB)));
  CALIB_BLOCK("fadd_nop_mulh_fadd", 8, MUL_PRE,
              NORVC(REPT(8, "fadd.s " FC ", " FB ", " FB "\n\tnop\n\tmulh t0, t1, t2\n\tfadd.s " FC
                            ", " FB ", " FB)));

  // The rs2 field of a fence or an ebreak against an FP operation writing an integer register.
  CALIB_BLOCK("fcvt_t6_fence", 8, FP_PRE, NORVC(REPT(8, "fcvt.w.s t6, " FB "\n\tfence")));
  CALIB_BLOCK("fcvt_t5_fence", 8, FP_PRE, NORVC(REPT(8, "fcvt.w.s t5, " FB "\n\tfence")));
  CALIB_BLOCK("fcvt_t0_fence_r_ow", 8, FP_PRE,
              NORVC(REPT(8, "fcvt.w.s t0, " FB "\n\tfence r, ow")));
  CALIB_BLOCK("fcvt_t6_fence_i", 8, FP_PRE, NORVC(REPT(8, "fcvt.w.s t6, " FB "\n\tfence.i")));
  CALIB_BLOCK_POST("fcvt_ra_ebreak", 8, FP_PRE "\n\t" TRAP_PRE,
                   NORVC(REPT(8, "fcvt.w.s ra, " FB "\n\tebreak")), TRAP_POST);
  CALIB_BLOCK_POST("fcvt_t5_ebreak", 8, FP_PRE "\n\t" TRAP_PRE,
                   NORVC(REPT(8, "fcvt.w.s t5, " FB "\n\tebreak")), TRAP_POST);

  // Events: APU_WB (0x8000) and APU_DEP (0x4000) on the held results, APU_TYPE (0x1000) behind the
  // mulh, APU_DEP on the fence.
  CALIB_HPM("sqrt_mulh_csr", 0x8000, MUL_PRE,
            NORVC(REPT(8, SQRT_NEG "\n\tmulh t0, t1, t2\n\tcsrrw a4, 0x340, a4")));
  CALIB_HPM("sqrt_mulh_use", 0x4000, MUL_PRE,
            NORVC(REPT(8, SQRT_NEG "\n\tmulh t0, t1, t2\n\tfadd.s " FC ", " FB ", " FB)));
  CALIB_HPM("fadd_mulh_fadd", 0x1000, MUL_PRE,
            NORVC(REPT(8, "fadd.s " FC ", " FB ", " FB "\n\tmulh t0, t1, t2\n\tfadd.s " FC ", " FB
                          ", " FB)));
  CALIB_HPM("fcvt_t6_fence", 0x4000, FP_PRE, NORVC(REPT(8, "fcvt.w.s t6, " FB "\n\tfence")));

  return 0;
}
