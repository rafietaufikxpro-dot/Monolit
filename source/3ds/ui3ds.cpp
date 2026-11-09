// Monolit 3DS - UI implementation (citro2d)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#include "ui3ds.h"

#include <citro2d.h>
#include <cstring>
#include <map>

namespace ui3ds {
const Col BG{11, 29, 41, 255}, PANEL{10, 23, 33, 255}, PANEL2{23, 56, 75, 255}, ACCENT{47, 208, 194, 255},
    WARN{255, 180, 84, 255}, BAD{255, 107, 107, 255}, TEXT{232, 244, 248, 255}, DIM{143, 176, 192, 255};

namespace {
C2D_Font* g_font = nullptr;
std::map<int, C2D_Font*> g_fonts;
}  // namespace

C2D_Font* font(int size) {
    auto it = g_fonts.find(size);
    if (it != g_fonts.end()) return it->second;
    C2D_Font* f = new C2D_Font(C2D_FontLoadSystem(size));
    g_fonts[size] = f;
    return f;
}

void init() {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_font = C2D_FontLoadSystem(16);
}

void shutdown() {
    for (auto& kv : g_fonts) {
        C2D_FontFree(*kv.second);
        delete kv.second;
    }
    g_fonts.clear();
    if (g_font) C2D_FontFree(g_font);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void begin() {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(gfxGetTopScreen(), BG.r / 255.0f, BG.g / 255.0f, BG.b / 255.0f, 1.0f);
    C2D_SceneBegin(gfxGetTopScreen());
}

void beginBottom() {
    C2D_TargetClear(gfxGetBottomScreen(), BG.r / 255.0f, BG.g / 255.0f, BG.b / 255.0f, 1.0f);
    C2D_SceneBegin(gfxGetBottomScreen());
}

void present() {
    C3D_FrameEnd(0);
}

void fill(int x, int y, int w, int h, Col c) {
    C2D_DrawRectSolid(x, y, 0, w, h, C2D_Color32(c.r, c.g, c.b, c.a));
}

void frame(int x, int y, int w, int h, Col c, int t) {
    for (int i = 0; i < t; i++) {
        C2D_DrawRectOutline(x + i, y + i, 0, w - 2 * i, h - 2 * i, 2, C2D_Color32(c.r, c.g, c.b, c.a));
    }
}

int textW(const std::string& s, int size) {
    float w = 0;
    for (char ch : s) {
        if (ch < 0x80) w += size * 0.6f;
        else w += size * 1.2f;
    }
    return (int)w;
}

void text(int x, int y, const std::string& s, int size, Col c, int align) {
    C2D_Font* f = font(size);
    if (!f) return;
    float tx = (float)x;
    if (align == 1) tx -= textW(s, size) / 2.0f;
    else if (align == 2) tx -= textW(s, size);
    C2D_DrawText(*f, C2D_WithColor, tx, (float)y, 0, 1.0f, 1.0f, C2D_Color32(c.r, c.g, c.b, c.a), s.c_str());
}

void topBar(const std::string& title, const std::string& right, Col rc) {
    fill(0, 0, W, 32, PANEL);
    fill(0, 31, W, 1, Col{31, 66, 86, 255});
    text(10, 6, title, 20, ACCENT);
    if (!right.empty()) text(W - 10, 8, right, 16, rc, 2);
}

void footBar(const std::vector<Hint>& hints) {
    int y = H - 32;
    fill(0, y, W, 32, PANEL);
    fill(0, y, W, 1, Col{31, 66, 86, 255});
    int x = 10;
    for (const Hint& h : hints) {
        int bw = (int)(textW(h.key, 18) + 20);
        fill(x, y + 2, bw, 20, Col{38, 71, 91, 255});
        text(x + bw / 2, y + 4, h.key, 18, TEXT, 1);
        text(x + bw + 6, y + 4, h.label, 16, DIM);
        x += bw + 6 + (int)textW(h.label, 16) + 16;
    }
}

void row(int idx, bool sel, const std::string& left, const std::string& right, Col rc) {
    int y = 40 + idx * 24;
    fill(10, y, W - 20, 22, sel ? Col{29, 74, 94, 255} : PANEL2);
    if (sel) frame(10, y, W - 20, 22, ACCENT, 2);
    int rw = right.empty() ? 0 : (int)textW(right, 16) + 12;
    text(20, y + 4, left, 18, TEXT);
    if (!right.empty()) text(W - 10, y + 4, right, 16, rc, 2);
}

void cell(int col, int rowi, bool sel, bool filled, const std::string& name, int level, int mark) {
    const int cw = 60, ch = 56, gap = 4;
    int x = 10 + col * (cw + gap);
    int y = 48 + rowi * (ch + gap);
    fill(x, y, cw, ch, filled ? PANEL2 : Col{18, 40, 54, 255});
    if (filled) {
        C2D_DrawCircleSolid(x + cw / 2, y + 18, 10, 16, C2D_Color32(100, 100, 100, 255));
        text(x + cw / 2, y + 34, name, 14, TEXT, 1);
        if (level >= 0) text(x + cw / 2, y + 48, "Lv." + std::to_string(level), 12, DIM, 1);
    } else {
        text(x + cw / 2, y + 22, "empty", 12, Col{90, 120, 135, 255}, 1);
    }
    if (mark) frame(x, y, cw, ch, mark == 1 ? WARN : BAD, 2);
    if (sel) frame(x - 2, y - 2, cw + 4, ch + 4, ACCENT, 2);
}

void toast(const std::string& msg) {
    if (msg.empty()) return;
    int w = std::min(W - 20, (int)textW(msg, 18) + 32);
    fill((W - w) / 2, H - 44, w, 24, Col{0, 0, 0, 215});
    frame((W - w) / 2, H - 44, w, 24, ACCENT, 1);
    text(W / 2, H - 38, msg, 16, TEXT, 1);
}

void dialog(const std::string& title, const std::string& body, const std::vector<Hint>& hints, Col accent) {
    C2D_TargetClear(gfxGetTopScreen(), 0, 0, 0, 0.7f);
    C2D_SceneBegin(gfxGetTopScreen());
    const int bw = 320, size = 18;
    int x = (W - bw) / 2, y = (H - 120) / 2;
    fill(x, y, bw, 120, Col{16, 37, 52, 255});
    frame(x, y, bw, 120, accent, 3);
    text(x + 12, y + 8, title, 20, accent);
    text(x + 12, y + 32, body, size, TEXT);
    int hx = x + 12, hy = y + 88;
    for (const Hint& h : hints) {
        int kw = (int)(textW(h.key, 16) + 16);
        fill(hx, hy, kw, 20, Col{38, 71, 91, 255});
        text(hx + kw / 2, hy + 2, h.key, 16, TEXT, 1);
        text(hx + kw + 4, hy + 2, h.label, 14, DIM);
        hx += kw + 4 + (int)textW(h.label, 14) + 12;
    }
}
}  // namespace ui3ds
