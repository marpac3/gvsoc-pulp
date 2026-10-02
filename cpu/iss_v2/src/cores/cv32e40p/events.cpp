// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/iss.hpp>

Cv32e40pEvents::Cv32e40pEvents(Iss &iss)
: Events(iss)
{
    // RTL FPU_ADDMUL_LAT and FPU_OTHERS_LAT, set by the core recipe.
    js::Config *conf = iss.get_js_config()->get("fpu_addmul_lat");
    this->fpu_addmul_lat = conf != NULL ? conf->get_int() : 0;
    conf = iss.get_js_config()->get("fpu_others_lat");
    this->fpu_others_lat = conf != NULL ? conf->get_int() : 0;
}

void Cv32e40pEvents::reset(bool active)
{
    Events::reset(active);
    if (active)
    {
        this->pending_events = 0;
        // The fetch from the boot address starts like a jump target.
        this->next_pc = 0;
        this->prev_dest_reg = -1;
        this->prev_load = false;
        this->wb_load_reg = -1;
        this->redirect = false;
        this->after_redirect = true;
        this->held_insn = NULL;
        this->hold_cycles = 0;
        this->jump_slack = 0;
        this->ex_slot = ExSlot();
        this->ex_prev = ExSlot();
        this->ex_enter_insn = NULL;
        this->ex_extra = 0;
        this->ex_misaligned = false;
        this->wb_load_any = -1;
        this->apu_nb_ops = 0;
        this->apu_lat_q = 0;
        this->apu_active_end = -1;
        this->apu_wb = -1;
        this->apu_insn = NULL;
        this->apu_type_start = this->apu_type_end = 0;
        this->apu_nack_start = this->apu_nack_end = 0;
    }
}
