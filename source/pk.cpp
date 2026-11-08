// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#include "pk.h"
#include "tables.h"
#include <cstring>
#include <vector>

static const uint8_t BLOCK_POS[24 * 4] = {
    0,1,2,3, 0,1,3,2, 0,2,1,3, 0,3,1,2,
    0,2,3,1, 0,3,2,1, 1,0,2,3, 1,0,3,2,
    2,0,1,3, 3,0,1,2, 2,0,3,1, 3,0,2,1,
    1,2,0,3, 1,3,0,2, 2,1,0,3, 3,1,0,2,
    2,3,0,1, 3,2,0,1, 1,2,3,0, 1,3,2,0,
    2,1,3,0, 3,1,2,0, 2,3,1,0, 3,2,1,0};

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// PokeCrypto.CryptArray: LCRNG-driven XOR over 16-bit words.
static void cryptArray(uint8_t* d, size_t len, uint32_t seed) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        seed = 0x41C64E6Du * seed + 0x6073u;
        uint16_t x = (uint16_t)(seed >> 16);
        d[i] ^= (uint8_t)x;
        d[i + 1] ^= (uint8_t)(x >> 8);
    }
}

bool pkSlotPresent(const uint8_t* slot) {
    // EntityDetection.IsPresent: non-zero encryption constant, or non-zero species.
    return rd32(slot) != 0 || rd16(slot + 8) != 0;
}

static void appendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

PkSummary pkSummarize(Fmt fmt, const uint8_t* slot) {
    PkSummary r;
    const FmtInfo* fi = fmtInfo(fmt);
    if (!fi || !pkSlotPresent(slot)) return r;
    r.present = true;

    std::vector<uint8_t> d(slot, slot + fi->slotSize);
    uint32_t ec = rd32(d.data());
    uint32_t sv = (ec >> 13) & 31;
    const int blockSize = (fmt == FMT_PA8) ? 0x58 : 0x50;
    const int storedEnd = 8 + 4 * blockSize;  // 0x148 or 0x168

    cryptArray(d.data() + 8, storedEnd - 8, ec);
    if (fi->slotSize > storedEnd) cryptArray(d.data() + storedEnd, fi->slotSize - storedEnd, ec);

    // Un-shuffle the four blocks: output block i = input block BLOCK_POS[sv*4+i].
    std::vector<uint8_t> tmp(d.begin() + 8, d.begin() + storedEnd);
    for (int i = 0; i < 4; i++) {
        int src = BLOCK_POS[(sv % 24) * 4 + i];
        memcpy(d.data() + 8 + i * blockSize, tmp.data() + src * blockSize, blockSize);
    }

    int species = rd16(d.data() + 8);
    if (fi->gen9Species) {
        int shift = species - 917;
        if (shift >= 0 && shift < TABLE9_LEN) species += TABLE9_INTERNAL_TO_NATIONAL[shift];
    }
    r.species = species;
    r.name = (species > 0 && species < SPECIES_COUNT) ? SPECIES_NAMES[species] : "???";
    if (fi->levelOff >= 0) r.level = d[fi->levelOff];

    for (int i = 0; i < 13; i++) {
        uint32_t cu = rd16(d.data() + fi->nickOff + i * 2);
        if (cu == 0) break;
        if (cu >= 0xD800 && cu < 0xDC00 && i + 1 < 13) {
            uint32_t lo = rd16(d.data() + fi->nickOff + (i + 1) * 2);
            if (lo >= 0xDC00 && lo < 0xE000) { cu = 0x10000 + ((cu - 0xD800) << 10) + (lo - 0xDC00); i++; }
        }
        appendUtf8(r.nickname, cu);
    }
    return r;
}

namespace {
// Gen 8 stored layout: 8 byte header + 4 shuffled blocks; PK8/PB8 blocks are 0x50 bytes, PA8 blocks 0x58 bytes.
// Anything after the stored part (party stats, PK8/PB8 only) is encrypted separately with the same seed.
struct Gen8Geom { int block, storedEnd; };
bool gen8Geom(Fmt fmt, Gen8Geom& g) {
    if (fmt == FMT_PK8 || fmt == FMT_PB8) { g = {0x50, 0x148}; return true; }
    if (fmt == FMT_PA8) { g = {0x58, 0x168}; return true; }
    return false;
}
}  // namespace

bool pkDecryptSlot(Fmt fmt, const uint8_t* slot, std::vector<uint8_t>& plain) {
    Gen8Geom g;
    if (!gen8Geom(fmt, g)) return false;
    const FmtInfo* fi = fmtInfo(fmt);
    if (!fi || !pkSlotPresent(slot)) return false;
    plain.assign(slot, slot + fi->slotSize);
    uint32_t ec = rd32(plain.data());
    uint32_t sv = (ec >> 13) & 31;
    cryptArray(plain.data() + 8, g.storedEnd - 8, ec);
    if (fi->slotSize > g.storedEnd) cryptArray(plain.data() + g.storedEnd, fi->slotSize - g.storedEnd, ec);
    std::vector<uint8_t> tmp(plain.begin() + 8, plain.begin() + g.storedEnd);
    for (int i = 0; i < 4; i++) memcpy(plain.data() + 8 + i * g.block, tmp.data() + BLOCK_POS[(sv % 24) * 4 + i] * g.block, g.block);
    return true;
}

void pkEncryptSlot(Fmt fmt, std::vector<uint8_t>& plain) {
    Gen8Geom g;
    const FmtInfo* fi = fmtInfo(fmt);
    if (!gen8Geom(fmt, g) || !fi || (int)plain.size() != fi->slotSize) return;
    uint32_t ec = rd32(plain.data());
    uint32_t sv = (ec >> 13) & 31;
    uint16_t sum = 0;
    for (int i = 8; i < g.storedEnd; i += 2) sum = (uint16_t)(sum + rd16(plain.data() + i));
    plain[6] = (uint8_t)sum;
    plain[7] = (uint8_t)(sum >> 8);
    std::vector<uint8_t> tmp(plain.begin() + 8, plain.begin() + g.storedEnd);
    // Inverse of the decrypt shuffle: plain block i goes to position BLOCK_POS[sv*4+i].
    for (int i = 0; i < 4; i++) memcpy(plain.data() + 8 + BLOCK_POS[(sv % 24) * 4 + i] * g.block, tmp.data() + i * g.block, g.block);
    cryptArray(plain.data() + 8, g.storedEnd - 8, ec);
    if (fi->slotSize > g.storedEnd) cryptArray(plain.data() + g.storedEnd, fi->slotSize - g.storedEnd, ec);
}
