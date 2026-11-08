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
#include "../source/sidedata.h"

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


// Builds an encrypted synthetic Legends: Arceus slot (PA8 layout, 0x168 bytes, no stat tail).
static std::vector<uint8_t> makeLA(uint16_t species, uint8_t level, uint8_t version, uint16_t met, uint8_t ball, bool alpha) {
    std::vector<uint8_t> p(0x168, 0);
    w32(p, 0, 0xB1C2D3E4u);
    w16(p, 0x08, species);
    w32(p, 0x0C, 0x0BADF00Du);
    w32(p, 0x10, conv::EXP_TABLE[conv::LA.rec[species].growth][level - 1]);
    p[0x16] = (uint8_t)(2 | (alpha ? 0x20 : 0));         // ability number 2, Alpha flag
    w32(p, 0x1C, 0xFEEDFACEu);
    p[0x20] = p[0x21] = 3;
    w32(p, 0x94, 31u | (30u << 5) | (29u << 10) | (28u << 15) | (27u << 20) | (26u << 25));
    const char16_t nick[] = u"Hisui";
    for (int i = 0; i < 5; i++) w16(p, 0x60 + i * 2, nick[i]);
    p[0xEE] = version; p[0xF2] = 2;
    w16(p, 0x13A, met);
    p[0x137] = ball;
    p[0x50] = 77; p[0x51] = 88; p[0x52] = alpha ? 255 : 99;
    w16(p, 0x54, 85); w16(p, 0x56, 98); w16(p, 0x58, 21); w16(p, 0x5A, 129);
    p[0x5C] = 15; p[0x5D] = 30; p[0x5E] = 20; p[0x5F] = 10;
    for (int i = 0; i < 6; i++) p[0xA4 + i] = (uint8_t)(i + 1);   // grit values
    pkEncryptSlot(FMT_PA8, p);
    return p;
}
static bool checksumLA(const std::vector<uint8_t>& p) {
    uint16_t sum = 0;
    for (int i = 8; i < 0x168; i += 2) sum = (uint16_t)(sum + r16(p, i));
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
        CHECK(conv::convertSlot(FMT_PK8, FMT_PK9, grookey.data(), out) == conv::UNSUPPORTED_FORMAT);
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

    // ---- side data: round trips restore the original game's own data (like HOME keeping each game's data)
    {
        // native Sword Pokemon with distinctive moves/ball/location, ability 4 (hidden)
        auto orig = make(FMT_PK8, 25, 0, 40, 44, 77, 7);
        { auto p = plain(FMT_PK8, orig); w16(p, 0x72, 85); w16(p, 0x74, 98); w16(p, 0x76, 21); w16(p, 0x78, 129);
          p[0x7A] = 15; p[0x7B] = 30; p[0x7C] = 20; p[0x7D] = 10; p[0x7E] = 2; w16(p, 0x82, 57); p[0x16] = 4; p[0x16] |= 0x10;
          w16(p, 0x120, 0); pkEncryptSlot(FMT_PK8, p); orig = p; }
        std::vector<uint8_t> mid, back;
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, orig.data(), mid) == conv::OK);
        bool restored = true;
        // without side data: reset (as before)
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, mid.data(), back, nullptr, &restored) == conv::OK && !restored);
        // with the original slot: moves, PP, PP Ups, relearn, ability, gigantamax flag, ball and location come back
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, mid.data(), back, orig.data(), &restored) == conv::OK && restored);
        auto a = plain(FMT_PK8, orig), b = plain(FMT_PK8, back);
        CHECK(memcmp(&a[0x72], &b[0x72], 0x18) == 0);        // moves, PP, PP Ups, relearn
        CHECK(a[0x16] == b[0x16] && memcmp(&a[0x14], &b[0x14], 2) == 0);
        CHECK(b[0x124] == 7 && r16(b, 0x122) == 77 && b[0xDE] == 44);
        CHECK(r16(b, 0x0A) == 0);                            // held item still removed
        CHECK(checksumOk(b));
        // a different Pokemon's data is never used
        auto other = make(FMT_PK8, 25, 0, 40, 44, 77, 7);
        { auto p = plain(FMT_PK8, other); w32(p, 0x1C, 0x11111111u); pkEncryptSlot(FMT_PK8, p); other = p; }
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, mid.data(), back, other.data(), &restored) == conv::OK && !restored);
    }
    {
        // Brilliant Diamond Pokemon -> Sword/Shield -> back to BD/SP
        auto orig = make(FMT_PB8, 25, 0, 30, 48 /*BD*/, 200, 11);
        { auto p = plain(FMT_PB8, orig); w16(p, 0x72, 85); w16(p, 0x74, 98); p[0x7A] = 15; p[0x7B] = 30; pkEncryptSlot(FMT_PB8, p); orig = p; }
        std::vector<uint8_t> sw, back;
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, orig.data(), sw) == conv::OK);
        auto s1 = plain(FMT_PK8, sw);
        CHECK(s1[0xDE] == 44 && r16(s1, 0x122) == 59999);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, sw.data(), back) == conv::FOREIGN_ORIGIN);          // no original data
        bool restored = false;
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, sw.data(), back, orig.data(), &restored) == conv::OK && restored);
        auto a = plain(FMT_PB8, orig), b = plain(FMT_PB8, back);
        CHECK(b[0xDE] == 48);                                  // original version is back
        CHECK(r16(b, 0x122) == 200 && b[0x124] == 11);         // original location and ball
        CHECK(memcmp(&a[0x72], &b[0x72], 0x18) == 0);          // original moves
        CHECK(memcmp(&a[0x1C], &b[0x1C], 4) == 0 && memcmp(&a[0x8C], &b[0x8C], 4) == 0 && memcmp(&a[0xF8], &b[0xF8], 26) == 0);
        CHECK(checksumOk(b));
        // Shining Pearl works the same way (magic location 59998)
        auto sp = make(FMT_PB8, 25, 0, 30, 49 /*SP*/, 150, 4);
        std::vector<uint8_t> sh, spBack;
        CHECK(conv::convertSlot(FMT_PB8, FMT_PK8, sp.data(), sh) == conv::OK);
        CHECK(plain(FMT_PK8, sh)[0xDE] == 45 && r16(plain(FMT_PK8, sh), 0x122) == 59998);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, sh.data(), spBack, sp.data(), &restored) == conv::OK);
        CHECK(plain(FMT_PB8, spBack)[0xDE] == 49);
        // other origins (e.g. Legends: Arceus placeholder) are still refused even if data were provided
        auto la = make(FMT_PK8, 25, 0, 10, 44, 60000);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PB8, la.data(), back, orig.data(), &restored) == conv::FOREIGN_ORIGIN);
    }

    // ---- Sword/Shield <-> Legends: Arceus (PK8 <-> PA8)
    {
        auto src = make(FMT_PK8, 25, 0, 25, 44, 12);
        std::vector<uint8_t> out;
        CHECK(conv::convertSlot(FMT_PK8, FMT_PA8, src.data(), out) == conv::OK);
        auto d = plain(FMT_PA8, out), s0 = plain(FMT_PK8, src);
        CHECK(d.size() == 0x168 && checksumLA(d));
        CHECK(memcmp(&d[0], &s0[0], 4) == 0 && memcmp(&d[0x1C], &s0[0x1C], 4) == 0 && memcmp(&d[0x94], &s0[0x8C], 4) == 0);   // EC, PID, IVs
        CHECK(memcmp(&d[0x60], &s0[0x58], 26) == 0);                                      // nickname (different offset)
        CHECK(r16(d, 0x14) == conv::LA.rec[25].abil[1] && (d[0x16] & 7) == 2 && !(d[0x16] & 0x60));   // ability, no Alpha/Noble
        CHECK(d[0xEE] == 44 && r16(d, 0x13A) == 12 && d[0x137] == 4);                    // version, location, ball kept
        CHECK(d[0x52] == d[0x50] && d[0xA4] == 0 && d[0xA9] == 0);                        // scale follows height scalar, no grit values
        CHECK(r16(d, 0x54) != 0 && r16(d, 0x8A) == 0 && r16(d, 0x0A) == 0);              // moves reset, relearn empty, no held item
        CHECK(d[0x5C] == conv::PP8A[r16(d, 0x54)]);
        const auto& r = conv::LA.rec[25];
        for (int m = 0; m < 4; m++) {
            uint16_t mv = r16(d, 0x54 + m * 2);
            if (!mv) continue;
            bool ok = false;
            for (int i = 0; i < r.learnCount; i++)
                if (conv::LA.learnMoves[r.learnOff + i] == mv && conv::LA.learnLevels[r.learnOff + i] <= 25) ok = true;
            CHECK(ok);
        }
        CHECK(r16(d, 0x92) > 0);                                                          // HP filled in
    }
    {
        // Legends: Arceus alpha -> Sword/Shield -> back, with and without the stored original
        auto orig = makeLA(25, 30, 47 /*PLA*/, 600, 30 /*a legend ball*/, true);
        std::vector<uint8_t> sw, back;
        CHECK(conv::convertSlot(FMT_PA8, FMT_PK8, orig.data(), sw) == conv::OK);
        auto d = plain(FMT_PK8, sw);
        CHECK(d[0xDE] == 44 && r16(d, 0x122) == 60000 && d[0x124] == 4);                 // PLA -> SW, placeholder location, ball sanitized
        CHECK(d[0x50] == 255 && d[0x51] == 255 && ((d[0x45] >> 3) & 1));                 // Alpha: mark + max size
        CHECK(r16(d, 0x72) != 0 && checksumOk(d));
        CHECK(conv::convertSlot(FMT_PK8, FMT_PA8, sw.data(), back) == conv::FOREIGN_ORIGIN);
        bool restored = false;
        CHECK(conv::convertSlot(FMT_PK8, FMT_PA8, sw.data(), back, orig.data(), &restored) == conv::OK && restored);
        auto a = plain(FMT_PA8, orig), b = plain(FMT_PA8, back);
        CHECK(b[0xEE] == 47 && r16(b, 0x13A) == 600 && b[0x137] == 30);                  // version, location, ball come back
        CHECK(memcmp(&a[0x54], &b[0x54], 0x10) == 0);                                    // original moves and PP
        CHECK(memcmp(&a[0xA4], &b[0xA4], 6) == 0 && (b[0x16] & 0x20));                  // grit values, Alpha
        CHECK(memcmp(&a[0x1C], &b[0x1C], 4) == 0 && checksumLA(b));
        // another origin's placeholder is never accepted for PA8
        auto sh = make(FMT_PK8, 25, 0, 20, 44, 59999);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PA8, sh.data(), back, orig.data(), &restored) == conv::FOREIGN_ORIGIN);
    }
    {
        std::vector<uint8_t> out;
        auto grookey = make(FMT_PK8, 810, 0, 10, 44, 12);
        CHECK(conv::convertSlot(FMT_PK8, FMT_PA8, grookey.data(), out) == conv::NOT_IN_GAME);   // not in Hisui
        auto bdsp = make(FMT_PB8, 25, 0, 20, 48, 12);
        CHECK(conv::convertSlot(FMT_PB8, FMT_PA8, bdsp.data(), out) == conv::UNSUPPORTED_FORMAT);  // BDSP <-> LA not implemented
        CHECK(conv::supported(FMT_PK8, FMT_PA8) && conv::supported(FMT_PA8, FMT_PK8) && !conv::supported(FMT_PA8, FMT_PB8));
    }
    // ---- SideStore file
    {
        const std::string path = "/tmp/monolit_sides_test.dat";
        remove(path.c_str()); remove((path + ".bak").c_str());
        SideStore st; std::string err;
        CHECK(st.load(path, err) && st.entries.empty());                      // missing file = empty store
        auto a = make(FMT_PK8, 25, 0, 40, 44, 77, 7), b = make(FMT_PB8, 133, 0, 20, 48, 12, 4);
        st.put(FMT_PK8, a.data()); st.put(FMT_PB8, b.data()); st.put(FMT_PK8, a.data());   // same identity twice -> one entry
        CHECK(st.entries.size() == 2);
        CHECK(st.save(path, err));
        SideStore st2;
        CHECK(st2.load(path, err) && st2.entries.size() == 2);
        SideKey k; CHECK(sideKeyOf(FMT_PK8, a.data(), k));
        const SideEntry* e = st2.find(FMT_PK8, k);
        const SideEntry* e2 = st2.find(FMT_PB8, k);  // the synthetic slots share one identity, so each format has its own entry
        CHECK(e && e->slot == a && e2 && e2->slot == b);
        // tampering is detected
        { FILE* f = fopen(path.c_str(), "r+b"); fseek(f, 30, SEEK_SET); int c = fgetc(f); fseek(f, 30, SEEK_SET); fputc(c ^ 1, f); fclose(f); }
        SideStore st3;
        CHECK(!st3.load(path, err) && st3.entries.empty());
        remove(path.c_str()); remove((path + ".bak").c_str());
    }
    printf(fails ? "\n%d FAILED\n" : "\nCONVERT TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
