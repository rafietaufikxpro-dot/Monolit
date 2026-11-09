// Monolit 3DS - PK6 slot crypto (from PKHeX PokeCrypto.Decrypt67)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>
#include "../games.h"

struct PkSummary3DS {
    bool present = false;
    int species = 0;
    int level = -1;
    std::string name;
    std::string nickname;
};

PkSummary3DS pk6Summarize3DS(const uint8_t* slot);
bool pk6SlotPresent3DS(const uint8_t* slot);
bool pk6DecryptSlot3DS(const uint8_t* slot, std::vector<uint8_t>& plain);
void pk6EncryptSlot3DS(std::vector<uint8_t>& plain);
