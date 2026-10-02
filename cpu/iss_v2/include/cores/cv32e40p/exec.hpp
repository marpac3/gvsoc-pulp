// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>

class Iss;
class Cv32e40pCosimModel;

class Cv32e40pExec : public ExecInOrder
{
public:
    Cv32e40pExec(Iss &iss) : ExecInOrder(iss) {}

    // Shadows ExecInOrder::start to create the co-simulation model (cosim.cpp).
    void start();

    inline bool can_switch_to_fast_mode();

    // At the retire of an instruction, the stall cycles it adds behind itself.
    inline int pending_stall_cycles() const { return this->stall_cycles; }

    // Co-simulation interface (cosim.hpp), inert until configured.
    Cv32e40pCosimModel *cosim = nullptr;
};
