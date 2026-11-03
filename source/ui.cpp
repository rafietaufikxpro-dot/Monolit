// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#include "ui.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cmath>
#include <map>

namespace ui {
const Col BG{11, 29, 41, 255}, PANEL{10, 23, 33, 255}, PANEL2{23, 56, 75, 255}, ACCENT{47, 208, 194, 255},
    WARN{255, 180, 84, 255}, BAD{255, 107, 107, 255}, TEXT{232, 244, 248, 255}, DIM{143, 176, 192, 255};

namespace {
SDL_Window* g_win = nullptr;
SDL_Renderer* g_ren = nullptr;
const void* g_fontData = nullptr;
size_t g_fontSize = 0;
std::map<int, TTF_Font*> g_fonts;
struct Tex { SDL_Texture* t; int w, h; };
std::map<std::string, Tex> g_cache;

TTF_Font* font(int size) {
    auto it = g_fonts.find(size);
    if (it != g_fonts.end()) return it->second;
    SDL_RWops* rw = SDL_RWFromConstMem(g_fontData, (int)g_fontSize);
    TTF_Font* f = rw ? TTF_OpenFontRW(rw, 1, size) : nullptr;
    g_fonts[size] = f;
    return f;
}

const Tex* getTex(const std::string& s, int size, Col c) {
    if (s.empty()) return nullptr;
    std::string key = std::to_string(size) + ":" + std::to_string((c.r << 16) | (c.g << 8) | c.b) + ":" + s;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return &it->second;
    TTF_Font* f = font(size);
    if (!f) return nullptr;
    SDL_Surface* sf = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{c.r, c.g, c.b, 255});
    if (!sf) return nullptr;
    if (g_cache.size() > 800) {  // keep memory bounded
        for (auto& kv : g_cache) SDL_DestroyTexture(kv.second.t);
        g_cache.clear();
    }
    Tex t{SDL_CreateTextureFromSurface(g_ren, sf), sf->w, sf->h};
    SDL_FreeSurface(sf);
    if (!t.t) return nullptr;
    return &(g_cache[key] = t);
}

void setCol(Col c) { SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a); }

// Fits text into maxW by trimming (UTF-8 safe) and adding "...".
std::string fit(const std::string& s, int size, int maxW) {
    if (textW(s, size) <= maxW) return s;
    std::string r = s;
    while (!r.empty()) {
        do r.pop_back(); while (!r.empty() && (r.back() & 0xC0) == 0x80);
        if (textW(r + "...", size) <= maxW) break;
    }
    return r + "...";
}

// Stable hue per name so each species gets its own generic colored dot (no official artwork).
Col dotColor(const std::string& n) {
    unsigned h = 2166136261u;
    for (unsigned char ch : n) { h ^= ch; h *= 16777619u; }
    float hue = (h % 360) / 60.0f, x = 1 - std::fabs(std::fmod(hue, 2.0f) - 1);
    float r = 0, g = 0, b = 0;
    int k = (int)hue;
    if (k == 0) { r = 1; g = x; } else if (k == 1) { r = x; g = 1; } else if (k == 2) { g = 1; b = x; }
    else if (k == 3) { g = x; b = 1; } else if (k == 4) { r = x; b = 1; } else { r = 1; b = x; }
    auto m = [](float v) { return (unsigned char)(70 + v * 150); };
    return Col{m(r), m(g), m(b), 255};
}

void disc(int cx, int cy, int rad, Col c) {
    setCol(c);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)std::sqrt((double)rad * rad - (double)dy * dy);
        SDL_RenderDrawLine(g_ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
}  // namespace

bool init(const void* fontData, size_t fontSize) {
    g_fontData = fontData;
    g_fontSize = fontSize;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    if (TTF_Init() != 0) return false;
    g_win = SDL_CreateWindow("Monolit", 0, 0, W, H, 0);
    if (!g_win) return false;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) return false;
    SDL_RenderSetLogicalSize(g_ren, W, H);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    return font(20) != nullptr;
}

void shutdown() {
    for (auto& kv : g_cache) SDL_DestroyTexture(kv.second.t);
    g_cache.clear();
    for (auto& kv : g_fonts) if (kv.second) TTF_CloseFont(kv.second);
    g_fonts.clear();
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    g_ren = nullptr; g_win = nullptr;
    TTF_Quit();
    SDL_Quit();
}

void begin() { setCol(BG); SDL_RenderClear(g_ren); }
void present() { SDL_RenderPresent(g_ren); }

void fill(int x, int y, int w, int h, Col c) { setCol(c); SDL_Rect r{x, y, w, h}; SDL_RenderFillRect(g_ren, &r); }
void frame(int x, int y, int w, int h, Col c, int t) {
    for (int i = 0; i < t; i++) { setCol(c); SDL_Rect r{x + i, y + i, w - 2 * i, h - 2 * i}; SDL_RenderDrawRect(g_ren, &r); }
}

int textW(const std::string& s, int size) {
    TTF_Font* f = font(size);
    int w = 0, h = 0;
    if (f && !s.empty()) TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

void text(int x, int y, const std::string& s, int size, Col c, int align) {
    const Tex* t = getTex(s, size, c);
    if (!t) return;
    int dx = align == 1 ? x - t->w / 2 : (align == 2 ? x - t->w : x);
    SDL_Rect d{dx, y, t->w, t->h};
    SDL_RenderCopy(g_ren, t->t, nullptr, &d);
}

void topBar(const std::string& title, const std::string& right, Col rc) {
    fill(0, 0, W, 64, PANEL);
    fill(0, 63, W, 1, Col{31, 66, 86, 255});
    text(40, 14, title, 30, ACCENT);
    text(W - 40, 20, fit(right, 22, 560), 22, rc, 2);
}

void footBar(const std::vector<Hint>& hints) {
    fill(0, H - 56, W, 56, PANEL);
    fill(0, H - 56, W, 1, Col{31, 66, 86, 255});
    int x = 40;
    for (const Hint& h : hints) {
        int bw = std::max(30, textW(h.key, 20) + 16);
        fill(x, H - 44, bw, 32, Col{38, 71, 91, 255});
        text(x + bw / 2, H - 42, h.key, 20, TEXT, 1);
        text(x + bw + 8, H - 42, h.label, 20, DIM);
        x += bw + 8 + textW(h.label, 20) + 26;
    }
}

void row(int idx, bool sel, const std::string& left, const std::string& right, Col rc) {
    int y = 80 + idx * 46;
    fill(40, y, W - 80, 40, sel ? Col{29, 74, 94, 255} : PANEL2);
    if (sel) frame(40, y, W - 80, 40, ACCENT, 2);
    int rw = right.empty() ? 0 : textW(right, 20) + 24;
    text(60, y + 6, fit(left, 24, W - 140 - rw), 24, TEXT);
    if (!right.empty()) text(W - 60, y + 9, right, 20, rc, 2);
}

void cell(int col, int rowi, bool sel, bool filled, const std::string& name, int level, int mark) {
    const int cw = 186, ch = 100, gap = 8;
    int x = 62 + col * (cw + gap), y = 78 + rowi * (ch + gap);
    fill(x, y, cw, ch, filled ? PANEL2 : Col{18, 40, 54, 255});
    if (filled) {
        disc(x + cw / 2, y + 28, 15, dotColor(name));
        text(x + cw / 2, y + 50, fit(name, 20, cw - 12), 20, TEXT, 1);
        if (level >= 0) text(x + cw / 2, y + 74, "Lv." + std::to_string(level), 17, DIM, 1);
    } else {
        text(x + cw / 2, y + 36, "kosong", 18, Col{90, 120, 135, 255}, 1);
    }
    if (mark) frame(x, y, cw, ch, mark == 1 ? WARN : BAD, 3);
    if (sel) frame(x - 2, y - 2, cw + 4, ch + 4, ACCENT, 3);
}

void toast(const std::string& msg) {
    if (msg.empty()) return;
    int w = std::min(W - 80, textW(msg, 22) + 48);
    fill((W - w) / 2, H - 108, w, 40, Col{0, 0, 0, 215});
    frame((W - w) / 2, H - 108, w, 40, ACCENT, 1);
    text(W / 2, H - 102, fit(msg, 22, w - 40), 22, TEXT, 1);
}

void dialog(const std::string& title, const std::string& body, const std::vector<Hint>& hints, Col accent) {
    fill(0, 0, W, H, Col{0, 0, 0, 190});
    const int bw = 860, size = 22, lh = 30;
    std::vector<std::string> lines;  // greedy word wrap, '\n' = paragraph break
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t e = body.find('\n', pos);
        std::string para = body.substr(pos, e == std::string::npos ? std::string::npos : e - pos), cur;
        size_t p = 0;
        while (p < para.size()) {
            size_t sp = para.find(' ', p);
            std::string w = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
            std::string t = cur.empty() ? w : cur + " " + w;
            if (!cur.empty() && textW(t, size) > bw - 80) { lines.push_back(cur); cur = w; } else cur = t;
            if (sp == std::string::npos) break;
            p = sp + 1;
        }
        lines.push_back(cur);
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    int bh = 70 + (int)lines.size() * lh + (hints.empty() ? 20 : 70);
    int x = (W - bw) / 2, y = std::max(30, (H - bh) / 2);
    fill(x, y, bw, bh, Col{16, 37, 52, 255});
    frame(x, y, bw, bh, accent, 3);
    text(x + 40, y + 20, title, 28, accent);
    for (size_t i = 0; i < lines.size(); i++) text(x + 40, y + 68 + (int)i * lh, lines[i], size, TEXT);
    int hx = x + 40, hy = y + bh - 52;
    for (const Hint& h : hints) {
        int kw = std::max(30, textW(h.key, 20) + 16);
        fill(hx, hy, kw, 32, Col{38, 71, 91, 255});
        text(hx + kw / 2, hy + 2, h.key, 20, TEXT, 1);
        text(hx + kw + 8, hy + 2, h.label, 20, DIM);
        hx += kw + 8 + textW(h.label, 20) + 30;
    }
}
}  // namespace ui
