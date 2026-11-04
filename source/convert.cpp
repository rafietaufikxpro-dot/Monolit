// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
//
// Derived from PKHeX (GPL-3.0): PKM/HOME/GameDataCore.cs (shared fields), GameDataPK8.cs / GameDataPB8.cs (per-game
// data: ability, ball, locations, move reset), HOME/LocationsHOME.cs, Locations8b.cs, PKM.ResetPartyStats.
#include "convert.h"

#include <cstring>

#include "conv_data.h"
#include "pk.h"

namespace conv {
namespace {
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return rd16(p) | ((uint32_t)rd16(p + 2) << 16); }
void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// G8PKM layout: bytes that HOME keeps in its shared "core" data (copied as-is). Everything else is per-game data
// (rebuilt below) or not carried over (held item, TR records, Dynamax data, PokeJob, ...) and stays zero.
struct Range { int off, len; };
const Range CORE[] = {
    {0x00, 4},    // encryption constant
    {0x08, 2},    // species
    {0x0C, 4},    // TID / SID
    {0x10, 4},    // EXP
    {0x18, 2},    // markings
    {0x1C, 4},    // PID
    {0x20, 2},    // nature, stat nature
    {0x24, 1},    // form
    {0x26, 12},   // EVs + contest stats
    {0x50, 2},    // height / weight scalar
    {0x58, 26},   // nickname
    {0x8C, 4},    // IVs, egg flag, nicknamed flag
    {0xA8, 26},   // handling trainer name
    {0xC2, 3},    // HT gender, HT language, current handler
    {0xC8, 6},    // HT friendship + memory
    {0xDE, 2},    // version, battle version
    {0xE2, 1},    // language
    {0xE4, 5},    // form argument, affixed ribbon
    {0xF8, 26},   // original trainer name
    {0x112, 7},   // OT friendship + memory
    {0x119, 6},   // egg / met dates
    {0x125, 2},   // met level + OT gender, hyper training
    {0x135, 8},   // HOME tracker (copied as-is: zero stays zero, never fabricated)
};

// GameVersion values (PKHeX) that HOME remaps to SW/SH.
enum { V_SW = 44, V_SH = 45, V_PLA = 47, V_BD = 48, V_SP = 49, V_SL = 50, V_VL = 51, V_FRX = 56, V_LGX = 57 };
enum : uint16_t { SHLG = 59994, SWFR, SHVL, SWSL, SHSP, SWBD, SWLA, SWSH_EGG = 65534 };
const uint16_t DEFAULT_8B_NONE = 65535, TRANSFER_PLACEHOLDER_0 = 658;
const uint8_t BALL_BEAST = 26, BALL_POKE = 4;

const GameTable& tableFor(Fmt f) { return f == FMT_PK8 ? SWSH : BDSP; }

bool hasForm(const PersonalRec& r, uint8_t form) { return form != 0 && r.formStatsIndex > 0 && form < r.formCount; }

int formIndex(const GameTable& t, uint16_t sp, uint8_t form) {
    if (sp > t.maxSpecies) return 0;
    const PersonalRec& r = t.rec[sp];
    return hasForm(r, form) ? r.formStatsIndex + form - 1 : sp;
}

uint16_t remappedVersion(uint8_t v, bool& remap) {
    remap = true;
    switch (v) {
        case V_PLA: case V_BD: case V_SL: case V_FRX: return V_SW;
        case V_SP: case V_VL: case V_LGX: return V_SH;
        default: remap = false; return v;
    }
}
uint16_t magicMet(uint8_t v) {
    switch (v) {
        case V_PLA: return SWLA; case V_BD: return SWBD; case V_SP: return SHSP; case V_SL: return SWSL;
        case V_VL: return SHVL; case V_FRX: return SWFR; case V_LGX: return SHLG; default: return 0;
    }
}

uint8_t levelFromExp(uint32_t exp, uint8_t growth) {
    const uint32_t* t = EXP_TABLE[growth > 5 ? 0 : growth];
    if (exp >= t[EXP_LEVELS - 1]) return 100;
    uint8_t lv = 1;
    while (exp >= t[lv]) ++lv;
    return lv;
}

// Learnset::SetEncounterMoves (ring buffer of the latest 4 distinct moves, then rotate into order).
void encounterMoves(const GameTable& t, int idx, int level, uint16_t m[4]) {
    const PersonalRec& r = t.rec[idx];
    int ctr = 0;
    for (int i = 0; i < r.learnCount; i++) {
        int req = t.learnLevels[r.learnOff + i];
        if (req < 1) continue;
        if (req > level) break;
        uint16_t mv = t.learnMoves[r.learnOff + i];
        if (m[0] == mv || m[1] == mv || m[2] == mv || m[3] == mv) continue;
        m[(ctr++) & 3] = mv;
    }
    if (ctr <= 4) return;
    int rot = ctr & 3;
    for (int k = 0; k < rot; k++) {
        uint16_t first = m[0];
        for (int j = 0; j < 3; j++) m[j] = m[j + 1];
        m[3] = first;
    }
}

uint8_t basePP(uint16_t move) { return move < PP8_COUNT ? PP8[move] : 0; }
}  // namespace

const char* describe(Result r) {
    switch (r) {
        case OK: return "OK";
        case SAME_FORMAT: return "Same format, no conversion needed";
        case UNSUPPORTED_FORMAT: return "Conversion between these games is not supported yet";
        case EMPTY: return "Slot is empty";
        case EGG: return "Eggs cannot be converted";
        case NOT_IN_GAME: return "This Pokémon does not exist in the destination game";
        case NOT_TRANSFERABLE: return "This species cannot be moved to/from BD/SP (HOME rule)";
        case FOREIGN_ORIGIN: return "This Pokémon was already transferred from another game (not supported yet)";
        case BAD_DATA: return "Invalid Pokémon data";
    }
    return "?";
}

bool supported(Fmt from, Fmt to) {
    return (from == FMT_PK8 && to == FMT_PB8) || (from == FMT_PB8 && to == FMT_PK8);
}

bool presentInGame(Fmt fmt, uint16_t species, uint8_t form) {
    if (fmt != FMT_PK8 && fmt != FMT_PB8) return false;
    const GameTable& t = tableFor(fmt);
    if (species == 0 || species > t.maxSpecies) return false;
    const PersonalRec& r = t.rec[species];
    if (form == 0) return r.present != 0;
    if (!hasForm(r, form)) return false;
    int idx = r.formStatsIndex + form - 1;
    return idx < t.count && t.rec[idx].present != 0;
}

Result convertSlot(Fmt from, Fmt to, const uint8_t* srcSlot, std::vector<uint8_t>& out) {
    if (from == to) return SAME_FORMAT;
    if (!supported(from, to)) return UNSUPPORTED_FORMAT;
    std::vector<uint8_t> s;
    if (!pkDecryptSlot(from, srcSlot, s)) return EMPTY;
    const FmtInfo* fi = fmtInfo(to);
    if (!fi || s.size() != (size_t)fi->slotSize) return BAD_DATA;

    const uint16_t species = rd16(&s[0x08]);
    const uint8_t form = s[0x24];
    if (species == 0 || rd32(&s[0]) == 0) return BAD_DATA;
    if ((rd32(&s[0x8C]) >> 30) & 1) return EGG;
    // PKHeX EntityConverter: Nincada (Shedinja clone paranoia) and Spinda (spot pattern/PID endianness) never cross BD/SP.
    if ((from == FMT_PB8 || to == FMT_PB8) && (species == 290 || species == 327)) return NOT_TRANSFERABLE;
    if (!presentInGame(to, species, form)) return NOT_IN_GAME;
    const uint16_t srcMet = rd16(&s[0x122]), srcEgg = rd16(&s[0x120]);
    if (from == FMT_PK8 && srcMet >= SHLG && srcMet <= SWLA) return FOREIGN_ORIGIN;

    const GameTable& t = tableFor(to);
    const int idx = formIndex(t, species, form);
    const PersonalRec& pr = t.rec[idx];
    std::vector<uint8_t> d(s.size(), 0);

    // 1) shared core data
    for (const Range& r : CORE) memcpy(&d[r.off], &s[r.off], r.len);
    // Ribbons / marks: HOME does not carry the unused bits (0x45 bit 7, 0x46, 0x47), and the two "has memory ribbon"
    // flags (0x38 bits 5-6) follow the memory ribbon counts. Masks verified against PKHeX (tools/golden_compare.py).
    static const uint8_t RIBBON_MASK[0x48 - 0x34] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,   // 0x34..0x3F
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00};                          // 0x40..0x47
    for (int o = 0x34; o < 0x48; o++) d[o] = s[o] & RIBBON_MASK[o - 0x34];
    d[0x3C] = s[0x3C]; d[0x3D] = s[0x3D];
    if (d[0x3C]) d[0x38] |= 0x20;
    if (d[0x3D]) d[0x38] |= 0x40;
    d[0x16] |= s[0x16] & 0x08;   // favorite flag
    d[0x22] = s[0x22] & 0x0D;    // fateful encounter + gender (not the unknown flag)

    // Size scalars (PKH.EnsureScaleSizeExists): Alpha mark forces 255/255; if both are zero HOME rolls new ones
    // (PokeSizeUtil.GetRandomScalar = rand(0..128) + rand(0..127)). We seed the roll from EC/PID so the result is stable.
    if ((d[0x45] >> 3) & 1) {
        d[0x50] = d[0x51] = 255;
    } else if (d[0x50] == 0 && d[0x51] == 0) {
        uint32_t x = (rd32(&d[0x00]) ^ (rd32(&d[0x1C]) * 2654435761u)) | 1u;
        auto rnd = [&x](uint32_t n) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x % n; };
        d[0x50] = (uint8_t)(rnd(0x81) + rnd(0x80));
        d[0x51] = (uint8_t)(rnd(0x81) + rnd(0x80));
    }

    // 2) per-game data rebuilt for the destination (PKHeX GameDataPK8/PB8 InitializeFrom + CopyTo)
    const uint8_t abilityNumber = s[0x16] & 7;
    int ai = abilityNumber >> 1;
    if (ai >= 3) ai = 0;
    wr16(&d[0x14], pr.abil[ai]);
    d[0x16] = (uint8_t)((d[0x16] & ~7) | abilityNumber);
    d[0x32] = s[0x32];           // pokerus state

    const uint8_t ball = s[0x124];
    d[0x124] = (to == FMT_PK8 && ball > BALL_BEAST) ? BALL_POKE : ball;

    if (to == FMT_PB8) {  // SwSh -> BDSP: "mainline" ids become BDSP-local ids
        auto local = [](uint16_t l) -> uint16_t { return l == 0 ? DEFAULT_8B_NONE : (l == TRANSFER_PLACEHOLDER_0 ? 0 : l); };
        wr16(&d[0x122], local(srcMet));
        wr16(&d[0x120], local(srcEgg));
    } else {              // BDSP -> SwSh
        auto mainline = [](uint16_t l) -> uint16_t { return l == 0 ? TRANSFER_PLACEHOLDER_0 : (l == DEFAULT_8B_NONE ? 0 : l); };
        uint16_t met = mainline(srcMet), egg = mainline(srcEgg);
        bool remap;
        uint16_t nv = remappedVersion(s[0xDE], remap);
        if (remap) {
            d[0xDE] = (uint8_t)nv;
            met = magicMet(s[0xDE]);
            egg = egg == 0 ? (uint16_t)0 : (egg > SWLA ? (uint16_t)SWSH_EGG : magicMet(s[0xDE]));
        }
        wr16(&d[0x122], met);
        wr16(&d[0x120], egg);
    }

    // 3) moves are reset to what the destination game would give at this level (relearn moves are cleared)
    const uint8_t level = levelFromExp(rd32(&s[0x10]), pr.growth);
    uint16_t mv[4] = {0, 0, 0, 0};
    encounterMoves(t, idx, level, mv);
    for (int i = 0; i < 4; i++) {
        wr16(&d[0x72 + i * 2], mv[i]);
        d[0x7A + i] = basePP(mv[i]);
    }

    // 4) party stats (PKM.ResetPartyStats): level, current HP = max HP, stats from destination base stats
    uint16_t st[6];
    const uint8_t ht = d[0x126];
    const int iv32 = (int)rd32(&d[0x8C]);
    // IV32 packs HP, ATK, DEF, SPE, SPA, SPD from bit 0, 5 bits each (same order as `ev` / `pr.stats` below).
    const int ivs[6] = {iv32 & 31, (iv32 >> 5) & 31, (iv32 >> 10) & 31, (iv32 >> 15) & 31, (iv32 >> 20) & 31, (iv32 >> 25) & 31};
    const int ev[6] = {d[0x26], d[0x27], d[0x28], d[0x29], d[0x2A], d[0x2B]};
    const int htBit[6] = {0, 1, 2, 5, 3, 4};  // HP ATK DEF SPE SPA SPD -> hyper training bit
    for (int i = 0; i < 6; i++) {
        int iv = ((ht >> htBit[i]) & 1) ? 31 : ivs[i];
        int base = pr.stats[i];
        st[i] = (uint16_t)(i == 0 ? (base == 1 ? 1 : ((iv + 2 * base + ev[i] / 4 + 100) * level / 100) + 10)
                                  : ((iv + 2 * base + ev[i] / 4) * level / 100) + 5);
    }
    const int nature = d[0x21];
    if (nature < 25 && nature / 5 != nature % 5) {
        st[nature / 5 + 1] = (uint16_t)(st[nature / 5 + 1] * 11 / 10);
        st[nature % 5 + 1] = (uint16_t)(st[nature % 5 + 1] * 9 / 10);
    }
    d[0x148] = level;
    wr16(&d[0x8A], st[0]);
    wr16(&d[0x14A], st[0]);
    wr16(&d[0x14C], st[1]);
    wr16(&d[0x14E], st[2]);
    wr16(&d[0x150], st[3]);
    wr16(&d[0x152], st[4]);
    wr16(&d[0x154], st[5]);

    pkEncryptSlot(to, d);
    out = d;
    return OK;
}
}  // namespace conv
