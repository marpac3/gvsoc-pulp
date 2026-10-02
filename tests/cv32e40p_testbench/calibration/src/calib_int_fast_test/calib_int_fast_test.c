// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// calib_int_test with every counter but mcycle inhibited, so that the model runs its fast
// handlers. The CALIB lines have the references of calib_int_test.
#define CALIB_FAST 1
#include "../calib_int_test/calib_int_test.c"
