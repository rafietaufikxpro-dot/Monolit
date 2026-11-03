// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// Host tests for source/convert.cpp (PK8 <-> PB8). No real save data is used; slots are synthetic.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../source/conv_data.h"
#include "../source/convert.h"
#include "../source/pk.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static uint16_t r16(const std::vector<uint8_t>& v, int o) { return (uint16_t)(v[o] | (v[o + 1] << 8)); }
static void w16(std::vector<uint8_t>& v, int o, uint16_t x) { v[o] = (uint8_t)x; v[o + 1] = (uint8_t)(x >> 8); }
static void w32(std::vector<uint8_t>& v, int o, uint32_t x) { w16(v, o, (uint16_t)x); w16(v, o + 2, (uint16_t)(x >> 16)); }

// Builds an encrypted synthetic slot.
static std::vector<uint8_t> make(Fmt f, uint16_t species, uint8_t form, uint8_t level, uint8_t version, uint16_t met, uint8_t ball = 4) {
    std::vector<uint8_t> p(0x158, 0);
    w32(p, 0, 0xA1B2C3D4u);               // EC
    w16(p, 0x08, species);
    w32(p, 0x0C, 0x12345678u);            // TID/SID
    uint8_t growth = (f == FMT_PK8 ? conv::SWSH : conv::BDSP).rec[species].growth;
    w32(p, 0x10, conv::EXP_TABLE[growth][level - 1]);
    p[0x16] = 2;                          // ability number 2 (slot index 1)
    w32(p, 0x1C, 0xCAFEBABEu);            // PID
    p[0x20] = p[0x21] = 3;                // nature Adamant (up ATK=0, down SPA=3)
    p[0x24] = form;
    p[0x26] = 4; p[0x27] = 252; p[0x29] = 252;
    w32(p, 0x8C, 31u | (30u << 5) | (29u << 10) | (28u << 15) | (27u << 20) | (26u << 25));
    const char16_t nick[] = u"Test";
    for (int i = 0; i < 4; i++) w16(p, 0x58 + i * 2, nick[i]);
    p[0xDE] = version;
    p[0xE2] = 2;
    w16(p, 0x122, met);
    p[0x124] = ball;
    w16(p, 0x72, 1); w16(p, 0x74, 2);     // old moves that must be reset
    pkEncryptSlot(f, p);
    return p;
}
static std::vector<uint8_t> plain(Fmt f, const std::vector<uint8_t>& slot) {
    std::vector<uint8_t> p;
    CHECK(pkDecryptSlot(f, slot.data(), p));
    return p;
}
static bool checksumOk(const std::vector<uint8_t>& p) {
    uint16_t sum = 0;
    for (int i = 8; i < 0x148; i += 2) sum = (uint16_t)(sum + r16(p, i));
    return sum == r16(p, 6);
}

int main() {
    // crypto round trip
    {
        auto s = make(FMT_PK8, 25, 0, 25, 44, 12);
        auto p = plain(FMT_PK8, s);
        CHECK(r16(p, 0x08) == 25 && checksumOk(p));
        CHECK(pkSummarize(FMT_PK8, s.data()).nickname == "Test");
    }
    // presence (same rule as PKHeX IsPresentInGame)
    CHECK(conv::presentInGame(FMT_PK8, 25, 0));
    CHECK(conv::presentInGame(FMT_PB8, 25, 0));
    CHECK(conv::presentInGame(FMT_PK8, 810, 0));      // Grookey: SwSh only
    CHECK(!conv::presentInGame(FMT_PB8, 810, 0));     // beyond BDSP range
    CHECK(!conv::presentInGame(FMT_PK8, 906, 0));     // Sprigatito: not in SwSh
    CHECK(!conv::presentInGame(FMT_PB8, 0, 0));

    // PK8 Pikachu lv25 -> PB8
    {
        auto src = make(FMT_PK8, 25, 0, 25, 44, 12);
        std::vector<uint8_t> out;
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, src.data(), out) == conv::OK);
        auto d = plain(FMT_PB8, out);
        auto s = plain(FMT_PK8, src);
        CHECK(checksumOk(d));
        CHECK(memcmp(&d[0], &s[0], 4) == 0 && memcmp(&d[0x1C], &s[0x1C], 4) == 0);   // EC, PID
        CHECK(memcmp(&d[0x8C], &s[0x8C], 4) == 0 && memcmp(&d[0x26], &s[0x26], 6) == 0);  // IVs, EVs
        CHECK(memcmp(&d[0x58], &s[0x58], 26) == 0);                                   // nickname
        const auto& pr = conv::BDSP.rec[25];
        CHECK(r16(d, 0x14) == pr.abil[1] && (d[0x16] & 7) == 2);
        CHECK(r16(d, 0x72) != 0 && !(r16(d, 0x72) == 1 && r16(d, 0x74) == 2));        // moves reset
        CHECK(d[0x7A] == conv::PP8[r16(d, 0x72)] && d[0x7E] == 0);
        CHECK(r16(d, 0x82) == 0);                                                     // relearn cleared
        CHECK(d[0x148] == 25 && r16(d, 0x14A) == r16(d, 0x8A) && r16(d, 0x14A) > 0);
        CHECK(r16(d, 0x0A) == 0);                                                     // held item dropped
        CHECK(d[0xDE] == 44);                                                         // origin version kept
        CHECK(r16(d, 0x122) == 12);
        // moves are all learnable at <= level 25 in BDSP's learnset
        const auto& r = conv::BDSP.rec[25];
        for (int m = 0; m < 4; m++) {
            uint16_t mv = r16(d, 0x72 + m * 2);
            if (!mv) continue;
            bool ok = false;
            for (int i = 0; i < r.learnCount; i++)
                if (conv::BDSP.learnMoves[r.learnOff + i] == mv && conv::BDSP.learnLevels[r.learnOff + i] <= 25) ok = true;
            CHECK(ok);
        }
    }
    // BD-origin PB8 -> PK8: version/location remap
    {
        auto src = make(FMT_PB8, 25, 0, 30, 48 /*BD*/, 0 /*mainline placeholder -> local 65535*/, 32 > 26 ? 27 : 4);
        std::vector<uint8_t> out;
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, src.data(), out) == conv::OK);
        auto d = plain(FMT_PK8, out);
        CHECK(d[0xDE] == 44);                    // BD -> SW
        CHECK(r16(d, 0x122) == 59999);           // SWBD magic location
        CHECK(d[0x124] == 4);                    // ball sanitized (27 > Beast)
        CHECK(checksumOk(d));
    }
    // non-BD origin PB8 keeps version; met 65535 -> mainline 0, 0 -> 658
    {
        auto src = make(FMT_PB8, 25, 0, 30, 44, 65535);
        std::vector<uint8_t> out;
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, src.data(), out) == conv::OK);
        auto d = plain(FMT_PK8, out);
        CHECK(d[0xDE] == 44 && r16(d, 0x122) == 0);
        auto src2 = make(FMT_PB8, 25, 0, 30, 44, 0);
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, src2.data(), out) == conv::OK);
        CHECK(r16(plain(FMT_PK8, out), 0x122) == 658);
    }
    // refusals
    {
        std::vector<uint8_t> out;
        auto grookey = make(FMT_PK8, 810, 0, 10, 44, 12);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, grookey.data(), out) == conv::NOT_IN_GAME);
        auto foreign = make(FMT_PK8, 25, 0, 10, 44, 59999);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, foreign.data(), out) == conv::FOREIGN_ORIGIN);
        auto egg = make(FMT_PK8, 25, 0, 1, 44, 12);
        { auto p = plain(FMT_PK8, egg); w32(p, 0x8C, (uint32_t)(p[0x8C] | (p[0x8D] << 8) | (p[0x8E] << 16) | ((uint32_t)p[0x8F] << 24)) | 0x40000000u); pkEncryptSlot(FMT_PK8, p); egg = p; }
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, egg.data(), out) == conv::EGG);
        std::vector<uint8_t> empty(0x158, 0);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, empty.data(), out) == conv::EMPTY);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PK8, grookey.data(), out) == conv::SAME_FORMAT);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PA8, grookey.data(), out) == conv::UNSUPPORTED_FORMAT);
    }
    // round trip keeps shared data
    {
        auto src = make(FMT_PK8, 25, 0, 40, 45, 20);
        std::vector<uint8_t> mid, back;
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, src.data(), mid) == conv::OK);
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, mid.data(), back) == conv::OK);
        auto a = plain(FMT_PK8, src), b = plain(FMT_PK8, back);
        CHECK(memcmp(&a[0x1C], &b[0x1C], 4) == 0 && memcmp(&a[0x8C], &b[0x8C], 4) == 0 && memcmp(&a[0xF8], &b[0xF8], 26) == 0);
        CHECK(a[0xDE] == b[0xDE] && r16(a, 0x122) == r16(b, 0x122));
    }
    printf(fails ? "\n%d FAILED\n" : "\nCONVERT TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
