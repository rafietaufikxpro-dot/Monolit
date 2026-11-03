// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
// Monolit UI layer: SDL2 + SDL2_ttf drawing helpers (Switch build only; no libnx here).
// The font is the Switch system shared font, passed in by main.cpp. No image/font assets are bundled.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace ui {
struct Col { unsigned char r, g, b, a; };
extern const Col BG, PANEL, PANEL2, ACCENT, WARN, BAD, TEXT, DIM;
struct Hint { std::string key, label; };

constexpr int W = 1280, H = 720;      // logical resolution (scaled for docked mode)
constexpr int LIST_ROWS = 12;         // visible rows in list screens

bool init(const void* fontData, size_t fontSize);
void shutdown();
void begin();                         // clear + background
void present();

void fill(int x, int y, int w, int h, Col c);
void frame(int x, int y, int w, int h, Col c, int thick = 2);
int textW(const std::string& s, int size);
// align: 0 left, 1 center, 2 right (x is the anchor)
void text(int x, int y, const std::string& s, int size, Col c, int align = 0);

void topBar(const std::string& title, const std::string& right, Col rightCol = DIM);
void footBar(const std::vector<Hint>& hints);
void row(int idx, bool sel, const std::string& left, const std::string& right, Col rightCol = DIM);
// One box slot: filled=false draws an empty slot. mark: 0 none, 1 added (pending), 2 removed (pending)
void cell(int col, int rowi, bool sel, bool filled, const std::string& name, int level, int mark);
void toast(const std::string& msg);
void dialog(const std::string& title, const std::string& body, const std::vector<Hint>& hints, Col accent = ACCENT);
}  // namespace ui
