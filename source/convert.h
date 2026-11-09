// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#pragma once
// Offline cross-game conversion, modelled on PKHeX's Pokemon HOME simulation (PKHeX is GPL-3.0).
// Phase 1: PK8 (Sword/Shield) <-> PB8 (Brilliant Diamond/Shining Pearl).
// This is a SIMULATION. Results carry no valid HOME tracker; use offline only.
#include <cstdint>
#include <string>
#include <vector>

#include "games.h"

namespace conv {
enum Result {
    OK = 0,
    SAME_FORMAT,         // nothing to convert (e.g. Sword -> Shield)
    UNSUPPORTED_FORMAT,  // pair not implemented yet
    EMPTY,
    EGG,                 // HOME does not accept eggs
    NOT_TRANSFERABLE,    // HOME rejects it outright (PB8 Nincada / Spinda)
    NOT_IN_GAME,         // species/form is not present in the destination game
    FOREIGN_ORIGIN,      // came into this game by conversion and its original data is not available
    BAD_DATA,
};

const char* describe(Result r);  // short English message for the UI

// True when conversion between the two formats is implemented.
bool supported(Fmt from, Fmt to);

// Scarlet/Violet only: the DLC a species needs (0 base game, 1 The Teal Mask, 2 The Indigo Disk), or -1 if unknown/none.
int dlcNeeded(Fmt fmt, uint16_t species, uint8_t form);

// Species/form flagged "present in game" in the destination game's personal table (same rule as PKHeX).
bool presentInGame(Fmt fmt, uint16_t species, uint8_t form);

// Converts one box slot (fmt-sized, encrypted) into an encrypted slot for `to`. `out` is only valid on OK.
// `origSide` (optional): the encrypted slot this same Pokemon had in the destination format on an earlier visit (see
// sidedata.h). When it matches, the destination game's own data (moves, ball, location, ability, version, ...) is
// restored from it instead of being rebuilt, and `*restored` is set.
Result convertSlot(Fmt from, Fmt to, const uint8_t* srcSlot, std::vector<uint8_t>& out, const uint8_t* origSide = nullptr, bool* restored = nullptr, int* dlcOut = nullptr);
}  // namespace conv
