// Monolit 3DS - UI layer (citro2d/citro3d, 3DS-only)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace ui3ds {
struct Col { unsigned char r, g, b, a; };
extern const Col BG, PANEL, PANEL2, ACCENT, WARN, BAD, TEXT, DIM;
struct Hint { std::string key, label; };

constexpr int W_TOP = 400, H_TOP = 240;
constexpr int W_BOT = 320, H_BOT = 240;
constexpr int W = 400, H = 240;
constexpr int LIST_ROWS = 8;

void init();
void shutdown();
void begin();
void beginBottom();
void present();

void fill(int x, int y, int w, int h, Col c);
void frame(int x, int y, int w, int h, Col c, int thick = 2);
int textW(const std::string& s, int size);
void text(int x, int y, const std::string& s, int size, Col c, int align = 0);

void topBar(const std::string& title, const std::string& right, Col rightCol = DIM);
void footBar(const std::vector<Hint>& hints);
void row(int idx, bool sel, const std::string& left, const std::string& right, Col rightCol = DIM);
void cell(int col, int rowi, bool sel, bool filled, const std::string& name, int level, int mark);
void toast(const std::string& msg);
void dialog(const std::string& title, const std::string& body, const std::vector<Hint>& hints, Col accent = ACCENT);
}  // namespace ui3ds
