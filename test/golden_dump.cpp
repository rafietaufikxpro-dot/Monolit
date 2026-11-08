// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// Dev tool: writes random synthetic plain slots + Monolit's converted output, so they can be diffed against
// PKHeX's EntityConverter (see tools/GOLDEN.md). Usage:
//   golden_dump <pk8|pb8|pk8:pa8|pa8:pk8> <count> <seed> <in.hex> <mine.hex>      (MAGIC=1: PK8 sources with a HOME placeholder location)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../source/conv_data.h"
#include "../source/convert.h"
#include "../source/pk.h"

static void hex(FILE* f, const std::vector<uint8_t>& v) { for (uint8_t b : v) fprintf(f, "%02X", b); fprintf(f, "\n"); }

// Field offsets of the source layout (PK8/PB8 = "G8PKM" layout, PA8 has its own).
struct Off { int iv, ver, met, egg, ball, ht, nick, htName, otName, lang, abil, flags, size; };
static const Off G8 = {0x8C, 0xDE, 0x122, 0x120, 0x124, 0x126, 0x58, 0xA8, 0xF8, 0xE2, 0x14, 0x16, 0x158};
static const Off A8 = {0x94, 0xEE, 0x13A, 0x138, 0x137, 0x13E, 0x60, 0xB8, 0x110, 0xF2, 0x14, 0x16, 0x168};

int main(int argc, char** argv) {
    if (argc < 6) return 2;
    std::string mode = argv[1];
    Fmt from, to;
    if (mode == "pk8") { from = FMT_PK8; to = FMT_PB8; }
    else if (mode == "pb8") { from = FMT_PB8; to = FMT_PK8; }
    else if (mode == "pk8:pa8") { from = FMT_PK8; to = FMT_PA8; }
    else if (mode == "pa8:pk8") { from = FMT_PA8; to = FMT_PK8; }
    else return 2;
    const Off& O = from == FMT_PA8 ? A8 : G8;
    const int n = atoi(argv[2]);
    std::mt19937 rng((unsigned)atoi(argv[3]));
    auto R = [&](uint32_t m) { return (uint32_t)(rng() % m); };
    FILE* fi = fopen(argv[4], "w");
    FILE* fm = fopen(argv[5], "w");
    const bool withLA = from == FMT_PA8 || to == FMT_PA8;
    int made = 0;
    while (made < n) {
        uint16_t sp = (uint16_t)(1 + R(withLA ? 898 : 493));
        uint8_t form = 0;
        const auto& r0 = conv::SWSH.rec[sp];
        if (r0.formCount > 1 && R(2)) form = (uint8_t)(1 + R(r0.formCount - 1));
        if (!conv::presentInGame(FMT_PK8, sp, form)) continue;
        if (withLA ? !conv::presentInGame(FMT_PA8, sp, form) : !conv::presentInGame(FMT_PB8, sp, form)) continue;
        std::vector<uint8_t> p(O.size);
        for (auto& b : p) b = (uint8_t)R(256);
        for (int i : {4, 5}) p[i] = 0;
        uint32_t ec = rng() | 1;
        memcpy(&p[0], &ec, 4);
        p[0x08] = (uint8_t)sp; p[0x09] = (uint8_t)(sp >> 8);
        p[0x24] = form;
        uint8_t lvl = (uint8_t)(1 + R(100));
        uint8_t growth = conv::SWSH.rec[sp].growth;
        uint32_t exp = conv::EXP_TABLE[growth][lvl - 1] + (lvl < 100 ? R(conv::EXP_TABLE[growth][lvl] - conv::EXP_TABLE[growth][lvl - 1]) : 0);
        memcpy(&p[0x10], &exp, 4);
        static const uint8_t ab[3] = {1, 2, 4};
        p[O.flags] = (uint8_t)((p[O.flags] & ~7) | ab[R(3)]);
        if (from == FMT_PA8) p[O.flags] &= 0x7F; else p[O.flags] &= 0x1F;   // keep to the flag bits that exist
        p[0x20] = p[0x21] = (uint8_t)R(25);
        p[0x22] = (uint8_t)(p[0x22] & 0x0D);
        p[0x0A] = p[0x0B] = 0;
        uint32_t iv32; memcpy(&iv32, &p[O.iv], 4); iv32 &= ~(3u << 30); memcpy(&p[O.iv], &iv32, 4);
        if (from == FMT_PA8) p[O.ver] = (uint8_t)(R(2) ? 47 : 44 + R(2));
        else p[O.ver] = from == FMT_PK8 ? (uint8_t)(44 + R(2)) : (uint8_t)(R(2) ? 44 + R(2) : 48 + R(2));
        { static const uint8_t langs[] = {1, 2, 3, 4, 5, 7, 8, 9, 10}; p[O.lang] = langs[R(9)]; }
        p[O.ball] = (uint8_t)(1 + R(from == FMT_PA8 ? 37 : 26));
        p[O.ht] &= 0x3F;
        uint16_t met = (uint16_t)R(300), egg = R(3) ? 0 : (uint16_t)R(300);
        if (from == FMT_PB8 && R(4) == 0) met = 65535;
        if (from == FMT_PK8 && getenv("MAGIC")) {
            static const uint16_t magics[] = {59994, 59995, 59996, 59997, 59998, 59999, 60000};
            met = magics[R(7)];
            uint32_t pick = R(3);
            egg = pick == 0 ? (uint16_t)0 : (pick == 1 ? (uint16_t)65534 : met);
            p[O.ver] = (uint8_t)(44 + R(2));
        }
        memcpy(&p[O.met], &met, 2); memcpy(&p[O.egg], &egg, 2);
        for (int base : {O.nick, O.htName, O.otName}) {   // 12 UTF-16 letters + terminator
            memset(&p[base], 0, 26);
            for (int i = 0; i < 12; i++) p[base + i * 2] = (uint8_t)('A' + R(26));
        }
        // Alignment / unused bytes are zero in real data (ExtraBytes of the format).
        if (from == FMT_PA8) {
            static const int extra[] = {0x17, 0x1A, 0x1B, 0x23, 0x33, 0x4C, 0x4D, 0x4E, 0x4F, 0x53, 0xAA, 0xAB, 0xD5, 0xD6, 0xD7, 0xF0, 0xF1, 0xF3, 0x12D, 0x13C};
            for (int o : extra) p[o] = 0;
            for (int o = 0xA0; o <= 0xA3; o++) p[o] = 0;
            for (int o = 0xB4; o <= 0xB7; o++) p[o] = 0;
            for (int o = 0xDE; o <= 0xEB; o++) p[o] = 0;
            for (int o = 0xF9; o <= 0x10F; o++) p[o] = 0;
            for (int o = 0x9C; o < 0xA0; o++) p[o] = 0;    // status condition
            for (int o = 0x165; o < 0x168; o++) p[o] = 0;
        } else {
            static const int extra[] = {0x17, 0x1A, 0x1B, 0x23, 0x33, 0x3E, 0x3F, 0x4C, 0x4D, 0x4E, 0x4F, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
                                        0x91, 0x92, 0x93, 0xC5, 0xE0, 0xE1, 0x115, 0x11F, 0x13D, 0x13E, 0x13F};
            for (int o : extra) p[o] = 0;
            for (int o = 0x9C; o <= 0xA7; o++) p[o] = 0;
            for (int o = 0xCE; o <= 0xDB; o++) p[o] = 0;
            for (int o = 0xE9; o <= 0xF7; o++) p[o] = 0;
            for (int o = 0x140; o <= 0x147; o++) p[o] = 0;
            memset(&p[0x94], 0, 4);
            p[0x125] = p[0x125];  // met level + OT gender: any value is legal
        }
        std::vector<uint8_t> slot = p;
        pkEncryptSlot(from, slot);
        std::vector<uint8_t> plainIn;
        if (!pkDecryptSlot(from, slot.data(), plainIn)) continue;  // canonical (checksum etc. refreshed)
        std::vector<uint8_t> out, plainOut;
        conv::Result res = conv::convertSlot(from, to, slot.data(), out);
        hex(fi, plainIn);
        if (res == conv::OK && pkDecryptSlot(to, out.data(), plainOut)) { fprintf(fm, "OK "); hex(fm, plainOut); }
        else fprintf(fm, "NULL %s\n", conv::describe(res));
        made++;
    }
    fclose(fi); fclose(fm);
    return 0;
}
