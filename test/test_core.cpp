// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
// Host-side tests for the platform independent core. Run: make -C .. test
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include "../source/hash.h"
#include "../source/sav.h"
#include "../source/bank.h"
#include "../source/pk.h"
#include "../source/swish_consts.h"
#include "../source/tables.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static std::string hex(const uint8_t* p, size_t n) { std::string s; char b[3]; for (size_t i = 0; i < n; i++) { snprintf(b, 3, "%02x", p[i]); s += b; } return s; }

// ---- independent-ish encoder used only by tests (mirrors PKHeX SCBlock.WriteBlock) ----
struct XSt {
    uint32_t st; int c;
    static uint32_t adv(uint32_t s) { s ^= s << 2; s ^= s >> 15; s ^= s << 13; return s; }
    XSt(uint32_t seed) : st(seed), c(0) { for (int i = __builtin_popcount(seed); i > 0; i--) st = adv(st); }
    uint8_t next() { uint8_t r = (uint8_t)(st >> (c * 8)); if (c == 3) { st = adv(st); c = 0; } else c++; return r; }
};
static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void encObject(std::vector<uint8_t>& out, uint32_t key, const std::vector<uint8_t>& data) {
    put32(out, key);
    XSt x(key);
    out.push_back(4 ^ x.next());
    uint32_t len = (uint32_t)data.size(), mask = 0;
    for (int i = 0; i < 4; i++) mask |= (uint32_t)x.next() << (8 * i);
    put32(out, len ^ mask);
    for (uint8_t b : data) out.push_back(b ^ x.next());
}
static void encBool(std::vector<uint8_t>& out, uint32_t key, int t) { put32(out, key); XSt x(key); out.push_back((uint8_t)(t ^ x.next())); }
static void encU32(std::vector<uint8_t>& out, uint32_t key, uint32_t v) {
    put32(out, key); XSt x(key); out.push_back(10 ^ x.next());
    for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)) ^ x.next());
}
static void encArrayU8(std::vector<uint8_t>& out, uint32_t key, const std::vector<uint8_t>& data) {
    put32(out, key); XSt x(key); out.push_back(5 ^ x.next());
    uint32_t mask = 0; for (int i = 0; i < 4; i++) mask |= (uint32_t)x.next() << (8 * i);
    put32(out, (uint32_t)data.size() ^ mask);
    out.push_back(8 ^ x.next());
    for (uint8_t b : data) out.push_back(b ^ x.next());
}
static void swishFinish(std::vector<uint8_t>& payload) {
    for (size_t i = 0; i < payload.size(); i++) payload[i] ^= SWISH_XORPAD[i % 0x7F];
    Sha256 s; s.init(); s.update(SWISH_INTRO, 64); s.update(payload.data(), payload.size()); s.update(SWISH_OUTRO, 64);
    uint8_t h[32]; s.final(h); payload.insert(payload.end(), h, h + 32);
}

static uint32_t rng = 12345;
static uint8_t rnd() { rng = rng * 1664525u + 1013904223u; return (uint8_t)(rng >> 24); }

static const uint8_t POS[24 * 4] = {0,1,2,3,0,1,3,2,0,2,1,3,0,3,1,2,0,2,3,1,0,3,2,1,1,0,2,3,1,0,3,2,2,0,1,3,3,0,1,2,2,0,3,1,3,0,2,1,1,2,0,3,1,3,0,2,2,1,0,3,3,1,0,2,2,3,0,1,3,2,0,1,1,2,3,0,1,3,2,0,2,1,3,0,3,1,2,0,2,3,1,0,3,2,1,0};

// Build an encrypted gen8/9 style slot from plain fields (test-only encoder).
static std::vector<uint8_t> makeSlot(Fmt fmt, uint32_t ec, uint16_t speciesRaw, uint8_t level, const char16_t* nick) {
    const FmtInfo* fi = fmtInfo(fmt);
    int bs = fmt == FMT_PA8 ? 0x58 : 0x50, storedEnd = 8 + 4 * bs;
    std::vector<uint8_t> pl(fi->slotSize, 0);
    for (int i = 0; i < 4; i++) pl[0] = 0;
    pl[0] = ec; pl[1] = ec >> 8; pl[2] = ec >> 16; pl[3] = ec >> 24;
    pl[8] = speciesRaw & 0xFF; pl[9] = speciesRaw >> 8;
    for (int i = 0; nick[i]; i++) { pl[fi->nickOff + i * 2] = nick[i] & 0xFF; pl[fi->nickOff + i * 2 + 1] = nick[i] >> 8; }
    if (fi->levelOff >= 0) pl[fi->levelOff] = level;
    // shuffle: enc block POS[sv*4+i] = plain block i
    uint32_t sv = (ec >> 13) & 31;
    std::vector<uint8_t> enc = pl;
    for (int i = 0; i < 4; i++) {
        int dst = POS[(sv % 24) * 4 + i];
        memcpy(&enc[8 + dst * bs], &pl[8 + i * bs], bs);
    }
    auto crypt = [&](size_t off, size_t len) {
        uint32_t seed = ec;
        for (size_t i = off; i + 1 < off + len; i += 2) { seed = 0x41C64E6Du * seed + 0x6073u; uint16_t x = seed >> 16; enc[i] ^= x & 0xFF; enc[i + 1] ^= x >> 8; }
    };
    crypt(8, storedEnd - 8);
    if (fi->slotSize > storedEnd) crypt(storedEnd, fi->slotSize - storedEnd);
    return enc;
}

static const GameDef* findGame(const char* name) { size_t n; const GameDef* g = gameDefs(n); for (size_t i = 0; i < n; i++) if (!strcmp(g[i].name, name)) return &g[i]; return nullptr; }

static std::vector<uint8_t> makeScSave(const GameDef& g, size_t extraAfter, std::vector<uint8_t>* plainBox) {
    size_t boxBytes = (size_t)g.boxes * g.perBox * fmtInfo(g.fmt)->slotSize;
    std::vector<uint8_t> box(boxBytes + 64, 0);
    plainBox->assign(box.begin(), box.end());
    std::vector<uint8_t> out;
    encBool(out, 0x11111111, 1);
    encU32(out, 0x22222222, 0xDEADBEEF);
    std::vector<uint8_t> junk(777); for (auto& b : junk) b = rnd();
    encObject(out, 0x33333333, junk);
    encArrayU8(out, 0x44444444, std::vector<uint8_t>(300, 7));
    encObject(out, g.boxKey, box);
    std::vector<uint8_t> tail(extraAfter); for (auto& b : tail) b = rnd();
    encObject(out, 0x55555555, tail);
    encBool(out, 0x66666666, 2);
    swishFinish(out);
    return out;
}

int main() {
    // ---- hashes ----
    {
        std::vector<uint8_t> d(1000); for (int i = 0; i < 1000; i++) d[i] = i % 251;
        Sha256 s; s.init(); s.update(d.data(), 1000); uint8_t h[32]; s.final(h);
        CHECK(hex(h, 32) == "4e4c294b331f7a2099a379bec34b9f9fc03dc46ab465d998f4d683da53487e6d");
        uint8_t m[16]; md5(d.data(), 1000, m); CHECK(hex(m, 16) == "a24f1e3ef66950e1327f210e3997ba2c");
        Sha256 a; a.init(); a.update((const uint8_t*)"abc", 3); a.final(h);
        CHECK(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        md5((const uint8_t*)"abc", 3, m); CHECK(hex(m, 16) == "900150983cd24fb0d6963f7d28e17f72");
        md5(d.data(), 55, m); CHECK(hex(m, 16) == "6912ee65fff2d9f9ce2508cddf8bcda0");
        md5(d.data(), 56, m); CHECK(hex(m, 16) == "51fdd1acda72405dfdfa03fcb85896d7");
        Sha256 b; b.init(); b.update(d.data(), 55); b.final(h);
        CHECK(hex(h, 32) == "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59");
        Sha256 c; c.init(); c.update(d.data(), 56); c.final(h);
        CHECK(hex(h, 32) == "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562");
    }

    // ---- pk summary ----
    {
        auto s = makeSlot(FMT_PK8, 0xA1B2C3D4, 133, 42, u"Fluffy");
        PkSummary p = pkSummarize(FMT_PK8, s.data());
        CHECK(p.present && p.species == 133 && p.name == "Eevee" && p.level == 42 && p.nickname == "Fluffy");
        // every shuffle value
        for (uint32_t sv = 0; sv < 32; sv++) {
            auto q = makeSlot(FMT_PK8, (sv << 13) | 0x1234, 25, 7, u"Pika");
            PkSummary r = pkSummarize(FMT_PK8, q.data());
            CHECK(r.species == 25 && r.level == 7 && r.nickname == "Pika");
        }
        auto a = makeSlot(FMT_PA8, 0x00ABCDEF, 133, 0, u"Eve");
        PkSummary pa = pkSummarize(FMT_PA8, a.data());
        CHECK(pa.species == 133 && pa.nickname == "Eve" && pa.level == -1);
        // gen9: internal 917+? uses table; check a known mapping consistent with the table
        int raw = 917 + 3; int expect = raw + TABLE9_INTERNAL_TO_NATIONAL[3];
        auto v = makeSlot(FMT_PK9, 0x00FF00FF, raw, 50, u"X");
        CHECK(pkSummarize(FMT_PK9, v.data()).species == expect);
        std::vector<uint8_t> empty(0x158, 0);
        CHECK(!pkSummarize(FMT_PK8, empty.data()).present);
    }

    // ---- SCBlock save: load, put, build, reload ----
    const char* scGames[] = {"Pokemon Sword", "Pokemon Scarlet", "Pokemon Legends: Arceus"};
    for (const char* name : scGames) {
        const GameDef* g = findGame(name);
        std::vector<uint8_t> plain;
        for (size_t extra : {5u, 333u, 1000u}) {
            auto file = makeScSave(*g, extra, &plain);
            SaveImage si; std::string err;
            CHECK(si.load(*g, file, err));
            if (!err.empty()) printf("  err: %s\n", err.c_str());
            CHECK(!si.present(0, 0));
            // unchanged rebuild must be byte identical
            std::vector<uint8_t> same; CHECK(si.build(same, err)); CHECK(same == file);
            // inject
            auto slot = makeSlot(g->fmt, 0x00001234 + extra, 150, 60, u"Mew");
            CHECK(si.put(3, 17, slot.data(), slot.size()));
            std::vector<uint8_t> out; CHECK(si.build(out, err)); CHECK(out.size() == file.size()); CHECK(out != file);
            SaveImage s2; CHECK(s2.load(*g, out, err));
            CHECK(s2.present(3, 17));
            CHECK(memcmp(s2.slot(3, 17), slot.data(), slot.size()) == 0);
            CHECK(!s2.present(3, 16) && !s2.present(0, 0));
            s2.clear(3, 17);
            std::vector<uint8_t> back; CHECK(s2.build(back, err)); CHECK(back == file);
            // corrupt hash -> refuse
            auto bad = out; bad[100] ^= 1; SaveImage s3; CHECK(!s3.load(*g, bad, err));
        }
    }

    // ---- BDSP ----
    {
        const GameDef* g = findGame("Pokemon Brilliant Diamond");
        std::vector<uint8_t> file(0xEF0A4); for (auto& b : file) b = rnd();
        memset(&file[0x14EF4], 0, 40 * 30 * 0x158);
        memset(&file[0xE9828 - 16], 0, 16);
        uint8_t h[16]; md5(file.data(), file.size(), h); memcpy(&file[0xE9828 - 16], h, 16);
        SaveImage si; std::string err;
        CHECK(si.load(*g, file, err));
        std::vector<uint8_t> same; CHECK(si.build(same, err)); CHECK(same == file);
        auto slot = makeSlot(FMT_PB8, 0x77, 4, 10, u"Char");
        si.put(39, 29, slot.data(), slot.size());
        std::vector<uint8_t> out; CHECK(si.build(out, err));
        SaveImage s2; CHECK(s2.load(*g, out, err)); CHECK(s2.present(39, 29));
        // only box bytes + hash changed
        size_t diff = 0; for (size_t i = 0; i < file.size(); i++) if (file[i] != out[i]) diff++;
        CHECK(diff <= 0x158 + 16);
        auto bad = out; bad[10] ^= 1; SaveImage s3; CHECK(!s3.load(*g, bad, err));
    }

    // ---- bank roundtrip ----
    {
        Bank b; BankEntry e; e.fmt = FMT_PK9; e.data.assign(0x158, 0xAB); b.entries.push_back(e);
        e.fmt = FMT_PA8; e.data.assign(0x168, 0xCD); b.entries.push_back(e);
        std::string err; CHECK(b.save("/tmp/test.bank", err)); CHECK(b.save("/tmp/test.bank", err));
        Bank c; CHECK(c.load("/tmp/test.bank", err)); CHECK(c.entries.size() == 2);
        CHECK(c.entries[0].fmt == FMT_PK9 && c.entries[1].data.size() == 0x168 && c.entries[1].data[5] == 0xCD);
        Bank d; CHECK(d.load("/tmp/does_not_exist.bank", err) && d.entries.empty());
    }


    // ---- bank: DLC tag, recovery from .bak, I/O errors are never "empty" ----
    {
        (void)!system("rm -rf /tmp/pbtest && mkdir -p /tmp/pbtest");
        const std::string P = "/tmp/pbtest/monolit.bank", B = P + ".bak";
        auto slurp = [](const std::string& p) { std::vector<uint8_t> v; FILE* f = fopen(p.c_str(), "rb"); if (!f) return v; uint8_t t[4096]; size_t n; while ((n = fread(t, 1, sizeof t, f)) > 0) v.insert(v.end(), t, t + n); fclose(f); return v; };
        auto mk = [](uint8_t fill, uint8_t dlc) { BankEntry e; e.fmt = FMT_PK9; e.dlc = dlc; e.data.assign(0x158, fill); return e; };
        std::string err;

        // dlc tag round trip; unknown tag values are preserved; tag 0 = unknown
        Bank a; a.entries = {mk(1, 0), mk(2, 1), mk(3, 3), mk(4, 9)};
        CHECK(a.save(P, err));
        Bank b; CHECK(b.load(P, err) && b.entries.size() == 4 && !b.recoveredFromBackup());
        CHECK(bankDlcNeeded(b.entries[0]) == -1 && bankDlcNeeded(b.entries[1]) == 0 && bankDlcNeeded(b.entries[2]) == 2 && bankDlcNeeded(b.entries[3]) == -1);
        CHECK(b.entries[3].dlc == 9);
        CHECK(b.save(P, err));
        Bank b2; CHECK(b2.load(P, err) && b2.entries[3].dlc == 9);

        // two saves in a row: .bak holds the previous version, main holds the new one
        Bank c; c.entries = {mk(7, 1)};
        CHECK(c.save(P, err));
        Bank cb; CHECK(cb.load(B, err) && cb.entries.size() == 4);
        Bank cm; CHECK(cm.load(P, err) && cm.entries.size() == 1);

        // main file vanished (power loss between the two renames): recover from .bak, keep .bak intact
        auto bakBefore = slurp(B);
        remove(P.c_str());
        Bank r; CHECK(r.load(P, err) && r.recoveredFromBackup() && r.entries.size() == 4);
        r.entries.push_back(mk(9, 2));
        CHECK(r.save(P, err));
        CHECK(slurp(B) == bakBefore);  // good backup untouched
        Bank r2; CHECK(r2.load(P, err) && !r2.recoveredFromBackup() && r2.entries.size() == 5);

        // main file damaged + valid .bak: recover; damaged main + no .bak: refuse (never empty)
        { FILE* f = fopen(P.c_str(), "wb"); fwrite("garbage", 1, 7, f); fclose(f); }
        Bank d; CHECK(d.load(P, err) && d.recoveredFromBackup() && d.entries.size() == 4);
        remove(B.c_str());
        Bank d2; err.clear(); CHECK(!d2.load(P, err) && !err.empty() && d2.entries.empty());

        // I/O error (path is a directory => open ok but read fails) must NOT become an empty bank
        (void)!system("rm -rf /tmp/pbtest/io && mkdir -p /tmp/pbtest/io/x.bank");
        Bank e; err.clear(); CHECK(!e.load("/tmp/pbtest/io/x.bank", err) && !err.empty() && e.entries.empty());

        // nothing at all: first run => empty, ok
        (void)!system("rm -rf /tmp/pbtest/none && mkdir -p /tmp/pbtest/none");
        Bank f; CHECK(f.load("/tmp/pbtest/none/x.bank", err) && f.entries.empty() && !f.recoveredFromBackup());
        CHECK(f.save("/tmp/pbtest/none/x.bank", err));
        // old-format file (reserved byte 0) still loads
        Bank g; CHECK(g.load("/tmp/pbtest/none/x.bank", err));
    }

    // ---- DLC level detection from blocks ----
    {
        struct Case { const char* game; std::vector<uint32_t> keys; int level; const char* name; };
        std::vector<Case> cases = {
            {"Pokemon Sword",    {},                           0, ""},
            {"Pokemon Shield",   {0x3F936BA9},                 1, "DLC Isle of Armor"},
            {"Pokemon Sword",    {0x3F936BA9, 0x3C9366F0},     2, "DLC Crown Tundra"},
            {"Pokemon Sword",    {0x3C9366F0},                 0, ""},   // R2 without R1: same as PKHeX (R1 checked first)
            {"Pokemon Scarlet",  {},                           0, ""},
            {"Pokemon Violet",   {0x100B93DA},                 1, "DLC Teal Mask"},
            {"Pokemon Scarlet",  {0x100B93DA, 0x66A33824},     2, "DLC Indigo Disk"},
            {"Pokemon Scarlet",  {0x66A33824},                 2, "DLC Indigo Disk"},
            {"Pokemon Legends: Arceus", {0x3F936BA9, 0x66A33824}, 0, ""},  // other games never report DLC
        };
        for (auto& cs : cases) {
            const GameDef* g = findGame(cs.game);
            size_t boxBytes = (size_t)g->boxes * g->perBox * fmtInfo(g->fmt)->slotSize;
            std::vector<uint8_t> out;
            encBool(out, 0x11111111, 1);
            for (uint32_t k : cs.keys) encObject(out, k, std::vector<uint8_t>(64, 0x5A));
            encObject(out, g->boxKey, std::vector<uint8_t>(boxBytes, 0));
            swishFinish(out);
            SaveImage si; std::string err;
            CHECK(si.load(*g, out, err));
            CHECK(si.dlcLevel() == cs.level);
            CHECK(std::string(dlcName(g->fmt, si.dlcLevel())) == cs.name);
        }
        // empty (zero length) DLC block does not count as present
        const GameDef* g = findGame("Pokemon Sword");
        std::vector<uint8_t> out;
        encObject(out, 0x3F936BA9, std::vector<uint8_t>());
        encObject(out, g->boxKey, std::vector<uint8_t>((size_t)g->boxes * g->perBox * 0x158, 0));
        swishFinish(out);
        SaveImage si; std::string err; CHECK(si.load(*g, out, err) && si.dlcLevel() == 0);
    }

    printf(fails ? "\n%d FAILED\n" : "\nALL TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
