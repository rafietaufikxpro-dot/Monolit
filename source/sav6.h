// Monolit 3DS - SaveImage6 (Gen 6 save)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>
#include "../games.h"

class SaveImage6 {
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
    bool build(std::vector<uint8_t>& out, std::string& err) const;
private:
    const GameDef* game_ = nullptr;
    int slotSize_ = 0;
    std::vector<uint8_t> file_;
    std::vector<uint8_t> box_;
};
