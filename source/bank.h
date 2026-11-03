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

struct BankEntry {
    Fmt fmt = FMT_NONE;
    // DLC tag of the save this Pokemon was deposited from: 0 = unknown (old entries),
    // otherwise 1 + DLC level (see SaveImage::dlcLevel). Stored in the header's formerly
    // reserved byte, so old and new files stay compatible. Unknown values are preserved.
    uint8_t dlc = 0;
    std::vector<uint8_t> data;  // raw encrypted slot bytes, exactly as stored in the save
};

// DLC level a Pokemon may need (0 = base game), or -1 if unknown.
inline int bankDlcNeeded(const BankEntry& e) { return (e.dlc >= 1 && e.dlc <= 3) ? e.dlc - 1 : -1; }

// Offline bank file. Layout:
//   "PKBANK01" | u32 count | u32 reserved
//   per entry: u8 fmt | u8 dlc | u16 size | size bytes
class Bank {
public:
    std::vector<BankEntry> entries;

    // Missing file (and no backup) => empty bank, ok. An unreadable or invalid file is an
    // ERROR (never treated as empty), unless a valid ".bak" exists, which is then loaded.
    bool load(const std::string& path, std::string& err);
    // Atomic: tmp + rename, keeps ".bak". When the bank was recovered from ".bak", the
    // (missing/invalid) main file is replaced without touching the good ".bak".
    bool save(const std::string& path, std::string& err) const;

    bool recoveredFromBackup() const { return recovered_; }

private:
    mutable bool recovered_ = false;
};
