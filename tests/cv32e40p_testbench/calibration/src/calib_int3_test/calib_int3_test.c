// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// Counters: the value read right after a write of mcycle, minstret and mhpmcounter3, the cycle
// event, and the counters across an mcountinhibit window (CALIB_READ lines).

#include "../calib_int_test/calib.h"

#define NORVC(s) ".option push\n\t.option norvc\n\t" s "\n\t.option pop"

#define EV(e)  "li t0, " #e "\n\tcsrw 0x323, t0"
#define NOP4   "nop\n\tnop\n\tnop\n\tnop"

int main(void)
{
  calib_init();

  // mcycle and minstret written then read.
  CALIB_READ("mcycle_w_r", "", NORVC("csrw 0xB00, x0\n\tcsrr %[v], 0xB00"));
  CALIB_READ("mcycle_w_nop4_r", "", NORVC("csrw 0xB00, x0\n\t" NOP4 "\n\tcsrr %[v], 0xB00"));
  CALIB_READ("minstret_w_r", "", NORVC("csrw 0xB02, x0\n\tcsrr %[v], 0xB02"));
  CALIB_READ("minstret_w_nop4_r", "", NORVC("csrw 0xB02, x0\n\t" NOP4 "\n\tcsrr %[v], 0xB02"));

  // mhpmcounter3 on the cycle line, on cycle and instr, and on instr only.
  CALIB_READ("hpm_cycle_w_r", EV(1), NORVC("csrw 0xB03, x0\n\tcsrr %[v], 0xB03"));
  CALIB_READ("hpm_cycle_w_nop4_r", EV(1), NORVC("csrw 0xB03, x0\n\t" NOP4 "\n\tcsrr %[v], 0xB03"));
  CALIB_READ("hpm_cycle_w_div_r", EV(1) "\n\tli t1, 100\n\tli t2, 7",
             NORVC("csrw 0xB03, x0\n\tdiv t3, t1, t2\n\tcsrr %[v], 0xB03"));
  CALIB_READ("hpm_cycle_instr_w_nop4_r", EV(3),
             NORVC("csrw 0xB03, x0\n\t" NOP4 "\n\tcsrr %[v], 0xB03"));
  CALIB_READ("hpm_instr_w_nop4_r", EV(2), NORVC("csrw 0xB03, x0\n\t" NOP4 "\n\tcsrr %[v], 0xB03"));
  CALIB_READ("hpm_cycle_w_r_r", EV(1),
             NORVC("csrw 0xB03, x0\n\tcsrr t1, 0xB03\n\tcsrr %[v], 0xB03"));
  CALIB_READ("hpm_cycle_user_alias", EV(1),
             NORVC("csrw 0xB03, x0\n\t" NOP4 "\n\tcsrr %[v], 0xC03"));

  // Selector changed while counting: cycle, then instr.
  CALIB_READ("hpm_cycle_then_instr", EV(1),
             NORVC("csrw 0xB03, x0\n\t" NOP4 "\n\tli t0, 2\n\tcsrw 0x323, t0\n\t" NOP4
                   "\n\tcsrr %[v], 0xB03"));

  // Inhibit windows: mhpmcounter3 (bit 3) and mcycle (bit 0) stopped across four nops.
  CALIB_READ("hpm_cycle_inhibit", EV(1),
             NORVC("csrw 0xB03, x0\n\tli t0, 8\n\tcsrw 0x320, t0\n\t" NOP4
                   "\n\tcsrw 0x320, x0\n\tnop\n\t" "csrr %[v], 0xB03"));
  CALIB_READ("mcycle_inhibit", "",
             NORVC("csrw 0xB00, x0\n\tcsrwi 0x320, 1\n\t" NOP4
                   "\n\tcsrw 0x320, x0\n\tnop\n\tcsrr %[v], 0xB00"));
  CALIB_READ("minstret_inhibit", "",
             NORVC("csrw 0xB02, x0\n\tcsrwi 0x320, 4\n\t" NOP4
                   "\n\tcsrw 0x320, x0\n\tnop\n\tcsrr %[v], 0xB02"));

  // The cycle line counted over the blocks of calib_int_test (compare with their mcycle cycles).
  CALIB_HPM("nop_cycles", 0x1, "", NORVC(REPT(64, "nop")));
  CALIB_HPM("branch_taken_cycles", 0x1, "", NORVC(REPT(64, "beq x0, x0, 1f\n1:")));
  CALIB_HPM("lw_use_cycles", 0x1, "", NORVC(REPT(32, "lw t0, 0(%[buf])\n\taddi t1, t0, 1")));
  CALIB_HPM("div_cycles", 0x1, "li t1, 100\n\tli t2, 7", NORVC(REPT(16, "div t0, t1, t2")));
  CALIB_HPM("nop_cycle_instr", 0x3, "", NORVC(REPT(64, "nop")));

  return 0;
}
