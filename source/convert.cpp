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

#include <cmath>
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

const GameTable& tableFor(Fmt f) { return f == FMT_PK8 ? SWSH : (f == FMT_PA8 ? LA : (f == FMT_PK9 ? SV : BDSP)); }

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
        case FOREIGN_ORIGIN: return "Converted from another game earlier, but its original data is not stored on this SD card";
        case BAD_DATA: return "Invalid Pokémon data";
    }
    return "?";
}

bool supported(Fmt from, Fmt to) {
    return (from == FMT_PK8 && to == FMT_PB8) || (from == FMT_PB8 && to == FMT_PK8) || (from == FMT_PK8 && to == FMT_PA8) ||
           (from == FMT_PA8 && to == FMT_PK8) || (from == FMT_PK8 && to == FMT_PK9) || (from == FMT_PK9 && to == FMT_PK8);
}

int dlcNeeded(Fmt fmt, uint16_t species, uint8_t form) {
    if (fmt != FMT_PK9 || species == 0 || species > SV.maxSpecies) return -1;
    const PersonalRec& base = SV.rec[species];
    int idx = species;
    if (form != 0 && hasForm(base, form)) idx = base.formStatsIndex + form - 1;
    uint8_t d = idx < SV.count ? SV.rec[idx].dlc : 255;
    if (d == 255) d = base.dlc;      // forms inherit the species' dex entry
    return d == 255 ? -1 : d;
}

bool presentInGame(Fmt fmt, uint16_t species, uint8_t form) {
    if (fmt != FMT_PK8 && fmt != FMT_PB8 && fmt != FMT_PA8 && fmt != FMT_PK9) return false;
    const GameTable& t = tableFor(fmt);
    if (species == 0 || species > t.maxSpecies) return false;
    const PersonalRec& r = t.rec[species];
    if (form == 0) return r.present != 0;
    if (!hasForm(r, form)) return false;
    int idx = r.formStatsIndex + form - 1;
    return idx < t.count && t.rec[idx].present != 0;
}


// ---------------------------------------------------------------- PK8 <-> PA8 (Sword/Shield <-> Legends: Arceus)
namespace {
// Shared core fields: offset in the PK8/PB8 layout (G8PKM), offset in PA8, length.
struct Map { int g8, pa8, len; };
const Map PA8_MAP[] = {
    {0x00, 0x00, 4},    // encryption constant
    {0x08, 0x08, 2},    // species
    {0x0C, 0x0C, 4},    // TID / SID
    {0x10, 0x10, 4},    // EXP
    {0x18, 0x18, 2},    // markings
    {0x1C, 0x1C, 4},    // PID
    {0x20, 0x20, 2},    // nature, stat nature
    {0x24, 0x24, 1},    // form
    {0x26, 0x26, 12},   // EVs + contest stats
    {0x50, 0x50, 2},    // height / weight scalar
    {0x58, 0x60, 26},   // nickname
    {0x8C, 0x94, 4},    // IVs, egg flag, nicknamed flag
    {0xA8, 0xB8, 26},   // handling trainer name
    {0xC2, 0xD2, 3},    // HT gender, HT language, current handler
    {0xC8, 0xD8, 6},    // HT friendship + memory
    {0xDE, 0xEE, 2},    // version, battle version
    {0xE2, 0xF2, 1},    // language
    {0xE4, 0xF4, 5},    // form argument, affixed ribbon
    {0xF8, 0x110, 26},  // original trainer name
    {0x112, 0x12A, 7},  // OT friendship + memory
    {0x119, 0x131, 6},  // egg / met dates
    {0x125, 0x13D, 2},  // met level + OT gender, hyper training
    {0x135, 0x14D, 8},  // HOME tracker
};
// Ribbon bytes 0x34..0x47 sit at the same offsets in both layouts; masks as in the PK8/PB8 path.
const uint8_t RIB_MASK_G8[0x14] = {0xFF, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00};
const uint8_t RIB_MASK_PA8[0x14] = {0xFF, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00};

// PokeSizeUtil / PA8.GetScalarPercent: plain float math (no fused multiply-add), exactly as the game computes it.
float scalarPercent(uint8_t scalar) {
    volatile float percent = scalar / 255.0f;
    volatile float scaled = percent * 0.40000004f;
    return scaled + 0.8f;
}
float heightAbs(const PersonalRec& p, uint8_t hs) { return (float)p.height * scalarPercent(hs); }
float weightAbs(const PersonalRec& p, uint8_t hs, uint8_t ws) {
    volatile float both = scalarPercent(hs) * scalarPercent(ws);
    return (float)p.weight * both;
}
void wrF(uint8_t* p, float f) { memcpy(p, &f, 4); }

// PA8.LoadStats HP: GetGanbaruStat + GetStatHp (floats as in the game; only the HP value is needed for the box slot).
int laHpMax(int base, int iv, int gv, int level) {
    static const uint8_t MUL[11] = {0, 2, 3, 4, 7, 8, 9, 14, 15, 16, 25};
    const int bias = iv >= 31 ? 3 : (iv >= 26 ? 2 : (iv >= 20 ? 1 : 0));
    const int mul = MUL[gv + bias > 10 ? 10 : gv + bias];
    const double step1 = std::sqrt((double)base) * mul;
    volatile float sum = (float)step1 + (float)level;
    const float result = sum / 2.5f;
    const int ganbaru = (int)std::round((double)result);
    volatile float lv = level / 100.0f;
    volatile float t = lv + 1.0f;
    volatile float t2 = t * (float)base;
    volatile float t3 = t2 + (float)level;
    return ganbaru + (int)t3;
}

// Stats shown for a PK8/PB8 slot (same formula as the existing G8 path).
}  // namespace

static Result convertPA8(Fmt from, Fmt to, const uint8_t* srcSlot, std::vector<uint8_t>& out, const uint8_t* origSide, bool* restored) {
    const bool toLA = (to == FMT_PA8);
    std::vector<uint8_t> s;
    if (!pkDecryptSlot(from, srcSlot, s)) return EMPTY;
    const FmtInfo* fi = fmtInfo(to);
    if (!fi) return BAD_DATA;
    // offsets in the SOURCE layout / DESTINATION layout
    const int sIV = toLA ? 0x8C : 0x94, sVer = toLA ? 0xDE : 0xEE, sMet = toLA ? 0x122 : 0x13A, sEgg = toLA ? 0x120 : 0x138, sBall = toLA ? 0x124 : 0x137;
    const int dVer = toLA ? 0xEE : 0xDE, dMet = toLA ? 0x13A : 0x122, dEgg = toLA ? 0x138 : 0x120, dBall = toLA ? 0x137 : 0x124;

    const uint16_t species = rd16(&s[0x08]);
    const uint8_t form = s[0x24];
    if (species == 0 || rd32(&s[0]) == 0) return BAD_DATA;
    if ((rd32(&s[sIV]) >> 30) & 1) return EGG;
    if (!presentInGame(to, species, form)) return NOT_IN_GAME;
    const uint16_t srcMet = rd16(&s[sMet]), srcEgg = rd16(&s[sEgg]);
    const bool magic = from == FMT_PK8 && srcMet >= SHLG && srcMet <= SWLA;

    std::vector<uint8_t> S;  // stored original of the destination game (HOME keeps per-game data)
    bool haveSide = false;
    if (origSide && pkDecryptSlot(to, origSide, S) && S.size() == (size_t)fi->slotSize && rd32(&S[0]) == rd32(&s[0]) &&
        rd32(&S[0x1C]) == rd32(&s[0x1C]) && rd16(&S[0x08]) == species && S[0x24] == form) {
        bool remap;
        const uint16_t expect = remappedVersion(S[dVer], remap);
        haveSide = (S[dVer] == s[sVer]) || (toLA && expect == s[sVer]);
    }
    if (magic && !haveSide) return FOREIGN_ORIGIN;
    if (magic && srcMet != SWLA) return FOREIGN_ORIGIN;  // only Legends: Arceus origins can come back to PA8

    const GameTable& t = tableFor(to);
    const int idx = formIndex(t, species, form);
    const PersonalRec& pr = t.rec[idx];
    std::vector<uint8_t> d(fi->slotSize, 0);

    // 1) shared core data
    for (const Map& m : PA8_MAP) {
        if (toLA) memcpy(&d[m.pa8], &s[m.g8], m.len);
        else memcpy(&d[m.g8], &s[m.pa8], m.len);
    }
    const uint8_t* rmask = toLA ? RIB_MASK_G8 : RIB_MASK_PA8;
    for (int o = 0x34; o < 0x48; o++) d[o] = s[o] & rmask[o - 0x34];
    d[0x3C] = s[0x3C]; d[0x3D] = s[0x3D];
    if (d[0x3C]) d[0x38] |= 0x20;
    if (d[0x3D]) d[0x38] |= 0x40;
    d[0x16] |= s[0x16] & 0x08;   // favorite flag
    d[0x22] = s[0x22] & 0x0D;    // fateful encounter + gender

    // Legends: Arceus keeps its own size byte; an Alpha Pokemon is shown as the Alpha mark and 255/255 in HOME.
    const bool srcAlpha = !toLA && (s[0x16] & 0x20);
    if (!toLA) d[0x50] = srcAlpha ? 255 : s[0x52];   // PKH.HeightScalar is overwritten by PA8's Scale on the way in
    if (srcAlpha) d[0x45] |= 0x08;
    if ((d[0x45] >> 3) & 1) {
        d[0x50] = d[0x51] = 255;
    } else if (d[0x50] == 0 && d[0x51] == 0) {
        uint32_t x = (rd32(&d[0x00]) ^ (rd32(&d[0x1C]) * 2654435761u)) | 1u;
        auto rnd = [&x](uint32_t n) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x % n; };
        d[0x50] = (uint8_t)(rnd(0x81) + rnd(0x80));
        d[0x51] = (uint8_t)(rnd(0x81) + rnd(0x80));
    }

    const uint8_t level = levelFromExp(rd32(&s[0x10]), pr.growth);
    if (haveSide) {
        // 2) restore the destination game's own data from the earlier visit
        auto isCore = [&](int o) {
            if (o >= 0x34 && o < 0x48) return true;
            for (const Map& m : PA8_MAP) {
                const int off = toLA ? m.pa8 : m.g8;
                if (o >= off && o < off + m.len) return true;
            }
            return false;
        };
        const int hpOff = toLA ? 0x92 : 0x8A, statusOff = toLA ? 0x9C : 0x94, end = toLA ? 0x168 : 0x148;
        for (int o = 0x08; o < end; o++) {
            if (isCore(o) || o == 0x0A || o == 0x0B || o == hpOff || o == hpOff + 1 || (o >= statusOff && o < statusOff + 4) || o == 0x16 || o == 0x22) continue;
            d[o] = S[o];
        }
        d[0x16] = (uint8_t)((S[0x16] & (toLA ? 0x77 : 0x17)) | (s[0x16] & 0x08));
        d[dVer] = S[dVer];
        if (restored) *restored = true;
    } else {
        // 2) per-game data rebuilt for the destination (PKHeX GameDataPA8 / GameDataPK8 InitializeFrom + CopyTo)
        const uint8_t abilityNumber = s[0x16] & 7;
        int ai = abilityNumber >> 1;
        if (ai >= 3) ai = 0;
        wr16(&d[0x14], pr.abil[ai]);
        d[0x16] = (uint8_t)((d[0x16] & ~7) | abilityNumber);
        d[0x32] = s[0x32];   // pokerus state
        const uint8_t ball = s[sBall];
        uint16_t met = srcMet, egg = srcEgg;
        if (toLA) {
            // Legends: Arceus origins must use a legend ball; other origins keep theirs
            d[dBall] = (s[sVer] == V_PLA && !(ball >= 28 && ball <= 37)) ? 28 : ball;
            d[0x52] = d[0x50];
        } else {
            d[dBall] = ball > BALL_BEAST ? BALL_POKE : ball;
            bool remap;
            const uint16_t nv = remappedVersion(s[sVer], remap);
            if (remap) {
                d[dVer] = (uint8_t)nv;
                met = magicMet(s[sVer]);
                egg = egg == 0 ? (uint16_t)0 : (egg > SWLA ? (uint16_t)SWSH_EGG : magicMet(s[sVer]));
            }
        }
        wr16(&d[dMet], met);
        wr16(&d[dEgg], egg);

        // 3) moves reset to the destination learnset at this level; relearn moves and PP Ups are cleared
        uint16_t mv[4] = {0, 0, 0, 0};
        encounterMoves(t, idx, level, mv);
        const int mOff = toLA ? 0x54 : 0x72, ppOff = toLA ? 0x5C : 0x7A;
        for (int i = 0; i < 4; i++) {
            wr16(&d[mOff + i * 2], mv[i]);
            const uint8_t* ppTable = toLA ? PP8A : PP8;
            const int ppCount = toLA ? PP8A_COUNT : PP8_COUNT;
            d[ppOff + i] = mv[i] < ppCount ? ppTable[mv[i]] : 0;
        }
    }

    // The Alpha mark only exists on Legends: Arceus Alphas, so a Pokemon carrying it is an Alpha there (PKHeX infers the same).
    if (toLA && ((d[0x45] >> 3) & 1)) d[0x16] |= 0x20;

    if (toLA) {
        // absolute size follows the scalars (recalculated from the Legends: Arceus personal data, never copied)
        if (!haveSide) {
            wrF(&d[0xAC], heightAbs(pr, d[0x50]));
            wrF(&d[0xB0], weightAbs(pr, d[0x50], d[0x51]));
        }
        // current HP = max HP (the box slot has no stat tail)
        const uint8_t ht = d[0x13E];
        const int iv32 = (int)rd32(&d[0x94]);
        const int iv = ((ht >> 0) & 1) ? 31 : (iv32 & 31);
        wr16(&d[0x92], (uint16_t)laHpMax(pr.stats[0], iv, d[0xA4], level));
    } else {
        // party stats as in the PK8/PB8 path
        uint16_t st[6];
        const uint8_t ht = d[0x126];
        const int iv32 = (int)rd32(&d[0x8C]);
        const int ivs[6] = {iv32 & 31, (iv32 >> 5) & 31, (iv32 >> 10) & 31, (iv32 >> 15) & 31, (iv32 >> 20) & 31, (iv32 >> 25) & 31};
        const int ev[6] = {d[0x26], d[0x27], d[0x28], d[0x29], d[0x2A], d[0x2B]};
        const int htBit[6] = {0, 1, 2, 5, 3, 4};
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
        for (int i = 0; i < 6; i++) wr16(&d[0x14A + i * 2], st[i]);
    }

    pkEncryptSlot(to, d);
    out = d;
    return OK;
}


// ---------------------------------------------------------------- PK8 <-> PK9 (Sword/Shield <-> Scarlet/Violet)
namespace {
const Map PK9_MAP[] = {
    {0x00, 0x00, 4},    // encryption constant
    {0x08, 0x08, 2},    // species (internal id == national id below 917)
    {0x0C, 0x0C, 4},    // TID / SID
    {0x10, 0x10, 4},    // EXP
    {0x18, 0x18, 2},    // markings
    {0x1C, 0x1C, 4},    // PID
    {0x20, 0x20, 2},    // nature, stat nature
    {0x24, 0x24, 1},    // form
    {0x26, 0x26, 12},   // EVs + contest stats
    {0x50, 0x48, 2},    // height / weight scalar
    {0x58, 0x58, 26},   // nickname
    {0x8C, 0x8C, 4},    // IVs, egg flag, nicknamed flag
    {0xA8, 0xA8, 26},   // handling trainer name
    {0xC2, 0xC2, 3},    // HT gender, HT language, current handler
    {0xC8, 0xC8, 6},    // HT friendship + memory
    {0xDE, 0xCE, 2},    // version, battle version
    {0xE2, 0xD5, 1},    // language
    {0xE4, 0xD0, 5},    // form argument, affixed ribbon
    {0xF8, 0xF8, 26},   // original trainer name
    {0x112, 0x112, 7},  // OT friendship + memory
    {0x119, 0x119, 6},  // egg / met dates
    {0x125, 0x125, 2},  // met level + OT gender, hyper training
    {0x135, 0x127, 8},  // HOME tracker
};
const uint8_t RIB_MASK_TO_PK9[0x14] = {0xFF, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00};
const uint8_t RIB_MASK_FROM_PK9[0x14] = {0xFF, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00};
}  // namespace

static Result convertPK9(Fmt from, Fmt to, const uint8_t* srcSlot, std::vector<uint8_t>& out, const uint8_t* origSide, bool* restored, int* dlcOut) {
    const bool toSV = (to == FMT_PK9);
    std::vector<uint8_t> s;
    if (!pkDecryptSlot(from, srcSlot, s)) return EMPTY;
    const FmtInfo* fi = fmtInfo(to);
    if (!fi) return BAD_DATA;
    // offsets in the source / destination layout
    const int sVer = toSV ? 0xDE : 0xCE, dVer = toSV ? 0xCE : 0xDE;
    const uint16_t species = rd16(&s[0x08]);
    const uint8_t form = s[0x24];
    if (species == 0 || rd32(&s[0]) == 0) return BAD_DATA;
    if ((rd32(&s[0x8C]) >> 30) & 1) return EGG;
    if (!presentInGame(to, species, form)) return NOT_IN_GAME;
    const uint16_t srcMet = rd16(&s[0x122]), srcEgg = rd16(&s[0x120]);
    const bool magic = from == FMT_PK8 && srcMet >= SHLG && srcMet <= SWLA;
    if (dlcOut) *dlcOut = dlcNeeded(to, species, form);

    std::vector<uint8_t> S;
    bool haveSide = false;
    if (origSide && pkDecryptSlot(to, origSide, S) && S.size() == (size_t)fi->slotSize && rd32(&S[0]) == rd32(&s[0]) &&
        rd32(&S[0x1C]) == rd32(&s[0x1C]) && rd16(&S[0x08]) == species && S[0x24] == form) {
        bool remap;
        const uint16_t expect = remappedVersion(S[dVer], remap);
        haveSide = (S[dVer] == s[sVer]) || (toSV && expect == s[sVer]);
    }
    if (magic && !haveSide) return FOREIGN_ORIGIN;
    if (magic && srcMet != SWSL && srcMet != SHVL) return FOREIGN_ORIGIN;  // only Scarlet/Violet origins can come back to PK9

    const GameTable& t = tableFor(to);
    const int idx = formIndex(t, species, form);
    const PersonalRec& pr = t.rec[idx];
    std::vector<uint8_t> d(fi->slotSize, 0);

    // 1) shared core data
    for (const Map& m : PK9_MAP) {
        if (toSV) memcpy(&d[m.pa8], &s[m.g8], m.len);
        else memcpy(&d[m.g8], &s[m.pa8], m.len);
    }
    const uint8_t* rmask = toSV ? RIB_MASK_TO_PK9 : RIB_MASK_FROM_PK9;
    for (int o = 0x34; o < 0x48; o++) d[o] = s[o] & rmask[o - 0x34];
    d[0x3C] = s[0x3C]; d[0x3D] = s[0x3D];
    if (d[0x3C]) d[0x38] |= 0x20;
    if (d[0x3D]) d[0x38] |= 0x40;
    d[0x16] |= s[0x16] & 0x08;   // favorite flag
    // fateful encounter is bit 0 in both; gender sits at bits 2-3 (PK8) but bits 1-2 (PK9)
    if (toSV) d[0x22] = (uint8_t)((s[0x22] & 1) | (((s[0x22] >> 2) & 3) << 1));
    else d[0x22] = (uint8_t)((s[0x22] & 1) | (((s[0x22] >> 1) & 3) << 2));

    // size scalars: PK9's Scale becomes the height scalar on the way in (PKH.HeightScalar is overwritten), and back
    const int hsOff = toSV ? 0x50 : 0x48, wsOff = toSV ? 0x51 : 0x49;  // in the source layout
    if (!toSV) d[0x50] = s[0x4A];
    (void)hsOff; (void)wsOff;
    if ((d[toSV ? 0x45 : 0x45] >> 3) & 1) {
        const int h = toSV ? 0x48 : 0x50, w = toSV ? 0x49 : 0x51;
        d[h] = d[w] = 255;
    } else {
        const int h = toSV ? 0x48 : 0x50, w = toSV ? 0x49 : 0x51;
        if (d[h] == 0 && d[w] == 0) {
            uint32_t x = (rd32(&d[0x00]) ^ (rd32(&d[0x1C]) * 2654435761u)) | 1u;
            auto rnd = [&x](uint32_t n) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x % n; };
            d[h] = (uint8_t)(rnd(0x81) + rnd(0x80));
            d[w] = (uint8_t)(rnd(0x81) + rnd(0x80));
        }
    }

    const uint8_t level = levelFromExp(rd32(&s[0x10]), pr.growth);
    if (haveSide) {
        auto isCore = [&](int o) {
            if (o >= 0x34 && o < 0x48) return true;
            for (const Map& m : PK9_MAP) {
                const int off = toSV ? m.pa8 : m.g8;
                if (o >= off && o < off + m.len) return true;
            }
            return o >= 0x11F && o < 0x120;   // obedience level follows the met level
        };
        const int statusOff = toSV ? 0x90 : 0x94;
        for (int o = 0x08; o < 0x148; o++) {
            if (isCore(o) || o == 0x0A || o == 0x0B || o == 0x8A || o == 0x8B || (o >= statusOff && o < statusOff + 4) || o == 0x16 || o == 0x22) continue;
            d[o] = S[o];
        }
        d[0x16] = (uint8_t)((S[0x16] & 0x07) | (s[0x16] & 0x08));
        d[dVer] = S[dVer];
        if (restored) *restored = true;
    } else {
        // 2) per-game data rebuilt for the destination (PKHeX GameDataPK9 / GameDataPK8 InitializeFrom + CopyTo)
        const uint8_t abilityNumber = s[0x16] & 7;
        int ai = abilityNumber >> 1;
        if (ai >= 3) ai = 0;
        wr16(&d[0x14], pr.abil[ai]);
        d[0x16] = (uint8_t)((d[0x16] & ~7) | abilityNumber);
        d[0x124] = s[0x124];
        uint16_t met = srcMet, egg = srcEgg;
        if (toSV) {
            d[0x4A] = d[0x48];                                   // Scale = height scalar
            d[0x94] = d[0x95] = pr.type1 != 0 ? pr.type1 : pr.type2;   // Tera Type import
        } else {
            d[0x32] = 0;                                         // SV data carries no pokerus state
            if (d[0x124] > BALL_BEAST) d[0x124] = BALL_POKE;
            bool remap;
            const uint16_t nv = remappedVersion(s[sVer], remap);
            if (remap) {
                d[dVer] = (uint8_t)nv;
                met = magicMet(s[sVer]);
                egg = egg == 0 ? (uint16_t)0 : (egg > SWLA ? (uint16_t)SWSH_EGG : magicMet(s[sVer]));
            }
        }
        wr16(&d[0x122], met);
        wr16(&d[0x120], egg);

        // 3) moves reset to the destination learnset at this level; relearn moves and PP Ups are cleared
        uint16_t mv[4] = {0, 0, 0, 0};
        encounterMoves(t, idx, level, mv);
        for (int i = 0; i < 4; i++) {
            wr16(&d[0x72 + i * 2], mv[i]);
            const uint8_t* ppTable = toSV ? PP9 : PP8;
            const int ppCount = toSV ? PP9_COUNT : PP8_COUNT;
            d[0x7A + i] = mv[i] < ppCount ? ppTable[mv[i]] : 0;
        }
    }
    if (toSV) d[0x11F] = d[0x125] & 0x7F;   // obedience level = met level

    // 4) party stats (same formula and layout for both)
    uint16_t st[6];
    const uint8_t ht = d[0x126];
    const int iv32 = (int)rd32(&d[0x8C]);
    const int ivs[6] = {iv32 & 31, (iv32 >> 5) & 31, (iv32 >> 10) & 31, (iv32 >> 15) & 31, (iv32 >> 20) & 31, (iv32 >> 25) & 31};
    const int ev[6] = {d[0x26], d[0x27], d[0x28], d[0x29], d[0x2A], d[0x2B]};
    const int htBit[6] = {0, 1, 2, 5, 3, 4};
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
    for (int i = 0; i < 6; i++) wr16(&d[0x14A + i * 2], st[i]);

    pkEncryptSlot(to, d);
    out = d;
    return OK;
}

Result convertSlot(Fmt from, Fmt to, const uint8_t* srcSlot, std::vector<uint8_t>& out, const uint8_t* origSide, bool* restored, int* dlcOut) {
    if (restored) *restored = false;
    if (dlcOut) *dlcOut = -1;
    if (from == to) return SAME_FORMAT;
    if (!supported(from, to)) return UNSUPPORTED_FORMAT;
    if (from == FMT_PA8 || to == FMT_PA8) return convertPA8(from, to, srcSlot, out, origSide, restored);
    if (from == FMT_PK9 || to == FMT_PK9) return convertPK9(from, to, srcSlot, out, origSide, restored, dlcOut);
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
    const bool magic = from == FMT_PK8 && srcMet >= SHLG && srcMet <= SWLA;  // already transferred from another game into SwSh

    // Stored per-game data from an earlier visit of this exact Pokemon to the destination game (HOME keeps this too).
    std::vector<uint8_t> S;
    bool haveSide = false;
    if (origSide && pkDecryptSlot(to, origSide, S) && S.size() == s.size() && rd32(&S[0]) == rd32(&s[0]) && rd32(&S[0x1C]) == rd32(&s[0x1C]) &&
        rd16(&S[0x08]) == species && S[0x24] == form) {
        bool remap;
        const uint16_t expect = remappedVersion(S[0xDE], remap);  // the version a SwSh copy of the original would carry
        haveSide = (S[0xDE] == s[0xDE]) || (to == FMT_PB8 && expect == s[0xDE]);
    }
    // Without the original data we cannot restore it, and PKHeX's fallback (keeping the HOME placeholder location and the
    // SW/SH version in a BD/SP save) is not something the game ever produces, so refuse instead.
    if (magic && !haveSide) return FOREIGN_ORIGIN;
    if (magic && !(srcMet == SWBD || srcMet == SHSP)) return FOREIGN_ORIGIN;  // other origins need their own formats

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

    const uint8_t level = levelFromExp(rd32(&s[0x10]), pr.growth);
    if (haveSide) {
        // 2+3) restore the destination game's own data from the earlier visit; shared fields keep their current values
        auto isCore = [](int o) {
            if (o >= 0x34 && o < 0x48) return true;  // ribbons (masked above)
            for (const Range& r : CORE) if (o >= r.off && o < r.off + r.len) return true;
            return false;
        };
        for (int o = 0x08; o < 0x148; o++) {
            if (isCore(o)) continue;
            if (o == 0x0A || o == 0x0B) continue;   // held item stays removed
            if (o == 0x8A || o == 0x8B) continue;   // current HP: recomputed
            if (o >= 0x94 && o < 0x98) continue;    // status condition
            if (o == 0x16 || o == 0x22) continue;   // flags: merged below
            d[o] = S[o];
        }
        d[0x16] = (uint8_t)((S[0x16] & 0x17) | (s[0x16] & 0x08));  // ability number + gigantamax from the old data, favorite stays
        d[0xDE] = S[0xDE];                                          // original game version
        if (restored) *restored = true;
    } else {
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
    uint16_t mv[4] = {0, 0, 0, 0};
    encounterMoves(t, idx, level, mv);
    for (int i = 0; i < 4; i++) {
        wr16(&d[0x72 + i * 2], mv[i]);
        d[0x7A + i] = basePP(mv[i]);
    }

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
