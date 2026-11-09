// Monolit 3DS - bank file MONO3D01 (3DS)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>
#include "../games.h"

struct Bank3DSEntry {
    Fmt fmt = FMT_NONE;
    std::vector<uint8_t> data;  // raw slot bytes
};

class Bank3DS {
public:
    std::vector<Bank3DSEntry> entries;
    bool load(const std::string& path, std::string& err);
    bool save(const std::string& path, std::string& err) const;
    bool recoveredFromBackup() const { return recovered_; }
private:
    mutable bool recovered_ = false;
};
