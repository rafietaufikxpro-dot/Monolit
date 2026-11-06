// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#pragma once
// "Side data" store: when a Pokemon is converted to another game, a copy of its original slot is kept here (like Pokemon HOME
// keeps each game's own data). If the Pokemon later returns to that game, convert.cpp restores moves, location, ball, ability
// and version from it. Portable (no libnx); file: sdmc:/switch/Monolit/sides.dat
#include <cstdint>
#include <string>
#include <vector>

#include "games.h"

struct SideKey {
    uint32_t ec = 0, pid = 0, tidsid = 0;
    bool operator==(const SideKey& o) const { return ec == o.ec && pid == o.pid && tidsid == o.tidsid; }
};

struct SideEntry {
    Fmt fmt = FMT_NONE;
    SideKey key;
    std::vector<uint8_t> slot;  // encrypted slot exactly as it was in the save
};

// Identity of a Gen 8 Pokemon (encryption constant, PID, TID/SID). False for other formats / empty slots.
bool sideKeyOf(Fmt fmt, const uint8_t* slot, SideKey& key);

class SideStore {
public:
    static const size_t MAX_ENTRIES = 3000;
    std::vector<SideEntry> entries;

    // A missing file is fine (empty store, returns true). Any damaged/unreadable file returns false and leaves the store empty.
    bool load(const std::string& path, std::string& err);
    // Writes <path>.tmp, reads it back, then swaps it in (keeps the previous file as <path>.bak).
    bool save(const std::string& path, std::string& err) const;

    void put(Fmt fmt, const uint8_t* slot);                          // replaces the entry for the same (fmt, identity)
    const SideEntry* find(Fmt fmt, const SideKey& key) const;
};
