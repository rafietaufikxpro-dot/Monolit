// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "games.h"

// In-memory view of one game's save file. Only the box area is ever modified;
// everything else in the file is carried through untouched.
class SaveImage {
public:
    bool load(const GameDef& g, std::vector<uint8_t> file, std::string& err);

    const GameDef* game() const { return game_; }
    int boxes() const { return game_ ? game_->boxes : 0; }
    int perBox() const { return game_ ? game_->perBox : 0; }
    int slotSize() const { return slotSize_; }

    const uint8_t* slot(int box, int idx) const;
    bool present(int box, int idx) const;
    bool put(int box, int idx, const uint8_t* data, size_t len);
    void clear(int box, int idx);

    // How many DLC packs the save has, detected like PKHeX does (presence of DLC blocks):
    //   Sword/Shield: 0 base, 1 Isle of Armor, 2 Crown Tundra
    //   Scarlet/Violet: 0 base, 1 Teal Mask (Kitakami), 2 Indigo Disk (Blueberry)
    // Other games have no DLC and always return 0.
    int dlcLevel() const;
    bool hasBlock(uint32_t key) const;  // SCBlock saves only; true if the block exists and has data

    // Produces the complete file with the box area replaced and hash/checksum recomputed.
    bool build(std::vector<uint8_t>& out, std::string& err) const;

private:
    const GameDef* game_ = nullptr;
    int slotSize_ = 0;
    std::vector<uint8_t> file_;     // original file
    std::vector<uint8_t> payload_;  // SCBlock: payload after static xor layer
    std::vector<uint8_t> box_;      // plaintext box area
    size_t dataOff_ = 0;            // SCBlock: offset of box block data in payload_
    uint32_t xsState_ = 0;          // SCBlock: xorshift state at block data start
    int xsCounter_ = 0;
    std::vector<std::pair<uint32_t, size_t>> blocks_;  // SCBlock: (key, data length) of every block
};

// Display name of a DLC level for the given Pokemon format ("" for level 0 / no DLC).
const char* dlcName(Fmt fmt, int level);
