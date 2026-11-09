// Monolit 3DS - PK7 slot crypto (same as PK6, PokeCrypto.Decrypt67)
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

PkSummary3DS pk7Summarize3DS(const uint8_t* slot);
bool pk7SlotPresent3DS(const uint8_t* slot);
bool pk7DecryptSlot3DS(const uint8_t* slot, std::vector<uint8_t>& plain);
void pk7EncryptSlot3DS(std::vector<uint8_t>& plain);
