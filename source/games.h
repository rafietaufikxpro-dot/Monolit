// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#pragma once
#include <cstddef>
#include <cstdint>

// Pokemon data formats. A bank entry can only be put back into a save of the same format.
enum Fmt : uint8_t { FMT_NONE = 0, FMT_PK8 = 1, FMT_PB8 = 2, FMT_PA8 = 3, FMT_PK9 = 4 };

struct FmtInfo {
    Fmt fmt;
    const char* name;
    int slotSize;      // bytes per slot as stored in the save's box area
    int nickOff;       // UTF-16LE nickname offset (13 chars)
    int levelOff;      // level byte offset, -1 if the slot does not carry it
    bool gen9Species;  // species uses the Gen 9 internal id table
};

inline const FmtInfo* fmtInfo(Fmt f) {
    static const FmtInfo t[] = {
        {FMT_PK8, "PK8 (Sword/Shield)",      0x158, 0x58, 0x148, false},
        {FMT_PB8, "PB8 (BD/SP)",             0x158, 0x58, 0x148, false},
        {FMT_PA8, "PA8 (Legends: Arceus)",   0x168, 0x60, -1,    false},
        {FMT_PK9, "PK9 (Scarlet/Violet)",    0x158, 0x58, 0x148, true},
    };
    for (const auto& i : t) if (i.fmt == f) return &i;
    return nullptr;
}

enum Family { FAM_SCBLOCK, FAM_BDSP };

struct GameDef {
    const char* name;
    uint64_t titleId;
    const char* saveFile;  // file name inside the save data
    Family family;
    Fmt fmt;
    uint32_t boxKey;       // SCBlock key of the box block (FAM_SCBLOCK)
    uint32_t boxOffset;    // fixed offset of box area (FAM_BDSP)
    int boxes;
    int perBox;
};

// Values from PKHeX (SaveBlockAccessor*, SAV8BS). Title IDs are the public application IDs.
inline const GameDef* gameDefs(size_t& count) {
    static const GameDef g[] = {
        {"Pokemon Sword",             0x0100ABF008968000ULL, "main",         FAM_SCBLOCK, FMT_PK8, 0x0d66012c, 0,       32, 30},
        {"Pokemon Shield",            0x01008DB008C2C000ULL, "main",         FAM_SCBLOCK, FMT_PK8, 0x0d66012c, 0,       32, 30},
        {"Pokemon Brilliant Diamond", 0x0100000011D90000ULL, "SaveData.bin", FAM_BDSP,    FMT_PB8, 0,          0x14EF4, 40, 30},
        {"Pokemon Shining Pearl",     0x010018E011D92000ULL, "SaveData.bin", FAM_BDSP,    FMT_PB8, 0,          0x14EF4, 40, 30},
        {"Pokemon Legends: Arceus",   0x01001F5010DFA000ULL, "main",         FAM_SCBLOCK, FMT_PA8, 0x47E1CEAB, 0,       32, 30},
        {"Pokemon Scarlet",           0x0100A3D008C5C000ULL, "main",         FAM_SCBLOCK, FMT_PK9, 0x0d66012c, 0,       32, 30},
        {"Pokemon Violet",            0x01008F6008C5E000ULL, "main",         FAM_SCBLOCK, FMT_PK9, 0x0d66012c, 0,       32, 30},
    };
    count = sizeof(g) / sizeof(g[0]);
    return g;
}
