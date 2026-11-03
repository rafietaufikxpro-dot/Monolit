// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#pragma once
#include <string>
#include <vector>
#include "games.h"

struct PkSummary {
    bool present = false;
    int species = 0;
    int level = -1;
    std::string name;      // species name
    std::string nickname;  // UTF-8
};

// Decrypts a COPY of the slot just to read species / level / nickname for display.
// Nothing here is ever written back to a save.
PkSummary pkSummarize(Fmt fmt, const uint8_t* slot);
bool pkSlotPresent(const uint8_t* slot);

// Slot-level crypto for Gen 8 boxes (PK8/PB8): full slotSize buffer, un-shuffled with the party tail decrypted.
// pkDecryptSlot: returns false for formats other than PK8/PB8 or empty slots.
bool pkDecryptSlot(Fmt fmt, const uint8_t* slot, std::vector<uint8_t>& plain);
// pkEncryptSlot: recomputes the checksum, shuffles and encrypts `plain` (same layout as pkDecryptSlot output).
void pkEncryptSlot(Fmt fmt, std::vector<uint8_t>& plain);
