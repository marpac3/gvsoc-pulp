// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// FPU timing: the APU dispatcher (cv32e40p_apu_disp.sv), the write port shared with the ALU and the
// LSU, the CSR accesses held behind the APU and the FP loads, the operands of the dependency check.

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#ifdef __riscv_zfinx
#define FA "t3"
#define FB "t4"
#define FC "t5"
#define FD "t6"
#define FE "a4"
#define R0 "x0"
#define FP_PRE "li t4, 0x3f800000\n\tli t5, 0x40400000\n\tli t6, 0x40000000"
#else
#define FA "ft0"
#define FB "ft1"
#define FC "ft2"
#define FD "ft3"
#define FE "ft4"
#define R0 "f0"
#define FP_PRE "li t1, 0x3f800000\n\tfmv.w.x ft1, t1\n\tli t1, 0x40400000\n\tfmv.w.x ft2, t1\n\t" \
               "li t1, 0x40000000\n\tfmv.w.x ft3, t1"
#endif

#define ADD(d, a, b) "fadd.s " d ", " a ", " b
#define DIV(d, a, b) "fdiv.s " d ", " a ", " b
#define NOP8 "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop"
#define ADDI20 REPT(20, "addi t1, t1, 1")

int main(void)
{
  calib_init();
#ifndef __riscv_zfinx
  // mstatus.FS = Initial: the F instructions are legal.
  __asm__ volatile("li t0, 0x2000\n\tcsrs 0x300, t0" ::: "t0");
#endif

  // The divider returns its result on the ALU write port: integer instructions behind it, writing a
  // register or not (sw), and a load writing on the other port.
  CALIB_BLOCK("fdiv_addi20", 4, FP_PRE, NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" ADDI20)));
  CALIB_BLOCK("fdiv_sw20", 4, FP_PRE,
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" REPT(20, "sw t1, 0(%[buf])"))));
  CALIB_BLOCK("fdiv_lw20", 4, FP_PRE,
              NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" REPT(20, "lw t2, 0(%[buf])"))));
  CALIB_BLOCK("fdiv_nop20", 4, FP_PRE, NORVC(REPT(4, DIV(FA, FB, FC) "\n\t" REPT(20, "nop"))));

  // Consumers of the divider result: an FP store, an FP compare, a move, and an independent FP op
  // of each latency class, a CSR read (fflags, not a status CSR) and an integer op behind it.
  CALIB_BLOCK("fdiv_use_fadd", 8, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" ADD(FD, FA, FC))));
  CALIB_BLOCK("fdiv_feq", 8, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\tfeq.s t1, " FB ", " FC)));
  CALIB_BLOCK("fdiv_fmin", 8, FP_PRE,
              NORVC(REPT(8, DIV(FA, FB, FC) "\n\tfmin.s " FD ", " FB ", " FC)));
  CALIB_BLOCK("fdiv_fcvt_w_s", 8, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\tfcvt.w.s t1, " FB)));
  CALIB_BLOCK("fdiv_csrr_fflags", 8, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\tcsrr t1, 0x001")));
  CALIB_BLOCK("fdiv_csrr_mscratch", 8, FP_PRE,
              NORVC(REPT(8, DIV(FA, FB, FC) "\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("fdiv_nop8_csrr", 8, FP_PRE,
              NORVC(REPT(8, DIV(FA, FB, FC) "\n\t" NOP8 "\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("fdiv_fsqrt", 8, FP_PRE, NORVC(REPT(8, DIV(FA, FB, FC) "\n\tfsqrt.s " FD ", " FB)));

  // Two FP ops in flight, then consumers of the first and of the second. Then FP ops behind an
  // integer op that writes in the cycle the FP result returns.
  CALIB_BLOCK("fadd2_use1", 16, FP_PRE,
              NORVC(REPT(16, ADD(FA, FB, FC) "\n\t" ADD(FD, FB, FC) "\n\t" ADD(FB, FA, FC))));
  CALIB_BLOCK("fadd2_use2", 16, FP_PRE,
              NORVC(REPT(16, ADD(FA, FB, FC) "\n\t" ADD(FD, FB, FC) "\n\t" ADD(FB, FD, FC))));
  CALIB_BLOCK("fadd_addi", 32, FP_PRE, NORVC(REPT(32, ADD(FA, FB, FC) "\n\taddi t1, t1, 1")));
  CALIB_BLOCK("fadd_addi2", 16, FP_PRE,
              NORVC(REPT(16, ADD(FA, FB, FC) "\n\taddi t1, t1, 1\n\taddi t2, t2, 1")));
  CALIB_BLOCK("fadd_lw", 32, FP_PRE, NORVC(REPT(32, ADD(FA, FB, FC) "\n\tlw t1, 0(%[buf])")));
  CALIB_BLOCK("fadd_lw_lw", 16, FP_PRE,
              NORVC(REPT(16, ADD(FA, FB, FC) "\n\tlw t1, 0(%[buf])\n\tlw t2, 4(%[buf])")));
  CALIB_BLOCK("fadd_mul", 32, FP_PRE "\n\tli t1, 3",
              NORVC(REPT(32, ADD(FA, FB, FC) "\n\tmul t2, t1, t1")));
  CALIB_BLOCK("fadd_csrr_mscratch", 16, FP_PRE,
              NORVC(REPT(16, ADD(FA, FB, FC) "\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("fadd_nop_csrr", 16, FP_PRE,
              NORVC(REPT(16, ADD(FA, FB, FC) "\n\tnop\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("fadd_fdiv", 8, FP_PRE, NORVC(REPT(8, ADD(FA, FB, FC) "\n\t" DIV(FD, FB, FC))));
  CALIB_BLOCK("fmin_fadd", 32, FP_PRE,
              NORVC(REPT(32, "fmin.s " FA ", " FB ", " FC "\n\t" ADD(FD, FB, FC))));

  // Operand registers of the dependency check: rs2 = 0 of the one-operand ops reads register 0 of
  // the FP register file (f0, x0 with Zfinx), and the FMA third operand.
  CALIB_BLOCK("fcvt_s_w_r0", 32, FP_PRE "\n\tli t1, 7",
              NORVC(REPT(32, ADD(R0, FB, FC) "\n\tfcvt.s.w " FE ", t1")));
  CALIB_BLOCK("fclass_r0", 32, FP_PRE, NORVC(REPT(32, ADD(R0, FB, FC) "\n\tfclass.s t1, " FB)));
  CALIB_BLOCK("fadd_r0_addi", 32, FP_PRE, NORVC(REPT(32, ADD(R0, FB, FC) "\n\taddi t1, t1, 1")));
  CALIB_BLOCK("fmadd_rs3", 16, FP_PRE,
              NORVC(REPT(16, ADD(FD, FB, FC) "\n\tfmadd.s " FA ", " FB ", " FC ", " FD)));

  // Loads and CSR accesses: a CSR access behind an FP load (integer load with Zfinx) or a store.
#ifndef __riscv_zfinx
  CALIB_BLOCK("flw_csrr_mscratch", 32, "",
              NORVC(REPT(32, "flw " FA ", 0(%[buf])\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("flw_nop_csrr", 32, "",
              NORVC(REPT(32, "flw " FA ", 0(%[buf])\n\tnop\n\tcsrr t1, 0x340")));
  CALIB_BLOCK("flw_fsw_use", 32, "",
              NORVC(REPT(32, "flw " FA ", 0(%[buf])\n\tfsw " FA ", 4(%[buf])")));
  CALIB_BLOCK("flw_fmv_x_w", 32, "", NORVC(REPT(32, "flw " FA ", 0(%[buf])\n\tfmv.x.w t1, " FA)));
  CALIB_BLOCK("fsw_csrr_mscratch", 32, FP_PRE,
              NORVC(REPT(32, "fsw " FB ", 4(%[buf])\n\tcsrr t1, 0x340")));
#else
  CALIB_BLOCK("lw_csrr_mscratch", 32, "",
              NORVC(REPT(32, "lw " FA ", 0(%[buf])\n\tcsrr t1, 0x340")));
  // A JALR whose target comes from an FP op (fsgnj.s copies the bits) at distance 0 and 1.
  CALIB_BLOCK("fsgnj_jalr", 16, "",
              NORVC(REPT(16, "la t2, 1f\n\tfsgnj.s t0, t2, t2\n\tjalr x0, 0(t0)\n1:")));
  CALIB_BLOCK("fsgnj_nop_jalr", 16, "",
              NORVC(REPT(16, "la t2, 1f\n\tfsgnj.s t0, t2, t2\n\tnop\n\tjalr x0, 0(t0)\n1:")));
  CALIB_BLOCK("feq_beq", 32, FP_PRE,
              NORVC(REPT(32, "feq.s t1, " FB ", " FC "\n\tbeq t1, x0, 1f\n1:")));
#endif

  // The status CSR access behind an FP op (frm write) and an fsqrt behind an fsqrt.
  CALIB_BLOCK("fadd_csrw_frm", 16, FP_PRE, NORVC(REPT(16, ADD(FA, FB, FC) "\n\tcsrw 0x002, x0")));
  CALIB_BLOCK("fsqrt_fsqrt_dep", 8, FP_PRE,
              NORVC(REPT(8, "fsqrt.s " FA ", " FD "\n\tfsqrt.s " FB ", " FA)));

  return 0;
}
