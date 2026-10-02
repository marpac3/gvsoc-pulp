// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <stdint.h>

/* Latency of fdiv.s and fsqrt.s in the FPU divider. With PulpDivsqrt = 0
 * (cv32e40p_fp_wrapper.sv), fpnew uses the T-Head E906 FDSU
 * (fpnew_divsqrt_th_32.sv, opene906 pa_fdsu_*.v). cycles() counts from the
 * acceptance (apu_req and apu_gnt) to the result (apu_rvalid), whatever the
 * rounding mode, and grant_wait() is the wait for apu_gnt.
 *
 * Special operands (ex1_srt_skip) take 2 cycles. With an exponent out of
 * range the iterations stop after the first one, which takes 4 cycles.
 * Otherwise the divider computes one radix-4 digit per cycle, until the
 * registered remainder is zero or 15 digits are done, then rounds and
 * registers the output, in digits + 3 cycles. A denormal divisor goes through
 * IDLE, WFI2 and ITER (pa_fdsu_ctrl.v), so apu_gnt comes one cycle later,
 * with the same latency. */
class Cv32e40pFdsu
{
public:
    static inline int cycles(bool sqrt, uint32_t a, uint32_t b);
    static inline int grant_wait(bool sqrt, uint32_t a, uint32_t b);

private:
    static inline bool special(uint32_t x);
    static inline bool denormal(uint32_t x);
    static inline void operand(uint32_t x, uint32_t &expnt, uint32_t &mant);
    static inline bool ex1_skip(bool sqrt, uint32_t a, uint32_t b);
};

/* Zero, infinity or NaN (pa_fpu_src_type.v). cnan is never set, because
 * pa_fpu_dp.v forces the upper bits to 1. */
inline bool Cv32e40pFdsu::special(uint32_t x)
{
    uint32_t e = (x >> 23) & 0xFF;
    uint32_t f = x & 0x7FFFFF;
    return (e == 0 && f == 0) || e == 0xFF;
}

inline bool Cv32e40pFdsu::denormal(uint32_t x)
{
    return ((x >> 23) & 0xFF) == 0 && (x & 0x7FFFFF) != 0;
}

/* The exponent (13 bits, frac_bin_val of pa_fdsu_ff1.v for a denormal) and
 * the 24-bit mantissa with its leading one. */
inline void Cv32e40pFdsu::operand(uint32_t x, uint32_t &expnt, uint32_t &mant)
{
    uint32_t e = (x >> 23) & 0xFF;
    uint32_t f = x & 0x7FFFFF;
    if (e != 0)
    {
        expnt = e;
        mant = (1u << 23) | f;
        return;
    }
    // k counts the leading zeros of the fraction, and the ff1 shift brings the first one
    // to the hidden bit.
    int k = 0;
    while (!((f << k) & 0x400000))
    {
        k++;
    }
    expnt = (uint32_t)(-k) & 0x1FFF;
    mant = (f << (k + 1)) & 0xFFFFFF;
}

/* ex1_srt_skip (pa_fdsu_special.v), set for a special operand or for the
 * square root of a negative number. */
inline bool Cv32e40pFdsu::ex1_skip(bool sqrt, uint32_t a, uint32_t b)
{
    if (sqrt)
    {
        return special(a) || (a >> 31);
    }
    return special(a) || special(b);
}

// fdsu_dn_stall = ctrl_sm_start && ex1_op1_id && div (pa_fdsu_ctrl.v, pa_fdsu_prepare.v).
inline int Cv32e40pFdsu::grant_wait(bool sqrt, uint32_t a, uint32_t b)
{
    return (!sqrt && !ex1_skip(sqrt, a, b) && denormal(b)) ? 1 : 0;
}

inline int Cv32e40pFdsu::cycles(bool sqrt, uint32_t a, uint32_t b)
{
    if (ex1_skip(sqrt, a, b))
    {
        return 2;
    }

    uint32_t e0, m0;
    uint32_t e1 = 127, m1 = 0;
    operand(a, e0, m0);
    if (!sqrt)
    {
        operand(b, e1, m1);
    }

    /* EX1 remainder (pa_fdsu_prepare.v). Its leading one is at bit 26 for a
     * division or the square root of an odd exponent, at bit 25 otherwise. */
    uint32_t rem = (sqrt && (e0 & 1)) ? (m0 << 2) : (m0 << 3);

    // EX2 exponent skip, overflow or underflow (pa_fdsu_srt_single.v).
    uint32_t er = ((e0 & 0x3FF) - (e1 & 0x3FF)) & 0x3FF;
    if (sqrt)
    {
        er = (er & 0x200) | (er >> 1);
    }
    bool er_neg = er & 0x200;
    bool overflow = !sqrt && !er_neg && ((er & 0x100) || ((er & 0x80) && (er & 0x7F)));
    bool underflow = er_neg && (er & 0x1FF) < 0x16A;
    if (overflow || underflow)
    {
        return 1 + 3;
    }

    // Bounds -K1 and -K2 of the digit selection, by bound_sel[3:0] (pa_fdsu_srt_single.v).
    static const uint8_t k1[16] = {0xF4, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9, 0xF9,
                                   0xF9, 0xF9, 0xF8, 0xF7, 0xF7, 0xF6, 0xF5, 0xF4};
    static const uint8_t k2[16] = {0xD1, 0xE7, 0xE7, 0xE7, 0xE7, 0xE7, 0xE7, 0xE7,
                                   0xE7, 0xE4, 0xE1, 0xDF, 0xDC, 0xD9, 0xD7, 0xD1};

    // total_qt_rt_30, total_qt_rt_minus_30 and qt_rt_const_shift_std.
    uint32_t qt = 0;
    uint32_t qtm = 0;
    uint32_t cst = 1u << 28;
    for (int n = 1; ; n++)
    {
        // srt_last_round, on the registered remainder (srt_cnt = 15 - n).
        if (rem == 0 || n == 15)
        {
            return n + 3;
        }

        bool neg = (rem >> 29) & 1;
        bool first = n == 1;

        // Digit selection, comparing qtrt_sel_rem with the bounds.
        uint32_t bound_sel = !sqrt ? (m1 >> 20) & 0xF : (first ? 0xA : (qt >> 25) & 0xF);
        uint32_t sel;
        if (first && sqrt)
        {
            sel = (((rem >> 29) & 1) << 7) | ((rem >> 21) & 0x7F);
        }
        else
        {
            sel = neg ? (~(rem >> 22)) & 0xFF : (rem >> 22) & 0xFF;
        }
        bool below_k1 = ((sel + k1[bound_sel]) & 0x80) != 0;
        bool below_k2 = ((sel + k2[bound_sel]) & 0x80) != 0;
        int digit = below_k1 ? 0 : (below_k2 ? 1 : 2);

        // Remainder update.
        uint32_t q1 = cst;
        uint32_t q2 = (cst << 1) & 0x3FFFFFFF;
        uint32_t add1, add2;
        if (!sqrt)
        {
            uint32_t d5 = m1 << 5;
            uint32_t d6 = m1 << 6;
            add1 = neg ? d5 : ~d5;
            add2 = neg ? d6 : ~d6;
        }
        else if (!neg)
        {
            add1 = ~(qt | (q1 >> 1));
            add2 = ~((qt << 1) | (q1 << 1));
        }
        else
        {
            add1 = qtm | (q1 << 1) | q1 | (q1 >> 1);
            add2 = (qtm << 1) | (q1 << 2) | (q1 << 1);
        }
        uint32_t shift = (rem & 0x80000000u) | ((rem & 0x1FFFFFFFu) << 2);
        uint32_t cin = neg ? 0 : 1;
        if (digit == 2)
        {
            rem = shift + add2 + cin;
        }
        else if (digit == 1)
        {
            rem = shift + add1 + cin;
        }
        else
        {
            rem = shift;
        }

        // On-the-fly quotient, which the square root reads back.
        uint32_t pre = neg ? qtm : qt;
        uint32_t qt_next, qtm_next;
        if (digit == 2)
        {
            qt_next = pre | q2;
            qtm_next = pre | q1;
        }
        else if (digit == 1)
        {
            qt_next = pre | (neg ? (q1 | q2) : q1);
            qtm_next = pre | (neg ? q2 : 0);
        }
        else
        {
            qt_next = qt;
            qtm_next = qtm | q1 | q2;
        }
        qt = qt_next;
        qtm = qtm_next;
        cst >>= 2;
    }
}
