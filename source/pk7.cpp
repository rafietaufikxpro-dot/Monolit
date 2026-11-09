// Monolit 3DS - PK7 slot crypto (same as PK6)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#include "pk7.h"
#include <cstring>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static const uint8_t BLOCK_POS[24 * 4] = {
    0,1,2,3, 0,1,3,2, 0,2,1,3, 0,3,1,2,
    0,2,3,1, 0,3,2,1, 1,0,2,3, 1,0,3,2,
    2,0,1,3, 3,0,1,2, 2,0,3,1, 3,0,2,1,
    1,2,0,3, 1,3,0,2, 2,1,0,3, 3,1,0,2,
    2,3,0,1, 3,2,0,1, 1,2,3,0, 1,3,2,0,
    2,1,3,0, 3,1,2,0, 2,3,1,0, 3,2,1,0};

static void cryptArray(uint8_t* d, size_t len, uint32_t seed) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        seed = 0x41C64E6Du * seed + 0x6073u;
        uint16_t x = (uint16_t)(seed >> 16);
        d[i] ^= (uint8_t)x;
        d[i + 1] ^= (uint8_t)(x >> 8);
    }
}

static void appendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

bool pk7SlotPresent3DS(const uint8_t* slot) {
    return rd32(slot) != 0 || rd16(slot + 8) != 0;
}

PkSummary3DS pk7Summarize3DS(const uint8_t* slot) {
    PkSummary3DS r;
    if (!pk7SlotPresent3DS(slot)) return r;
    r.present = true;

    std::vector<uint8_t> d(slot, slot + 0xE8);
    uint32_t ec = rd32(d.data());
    uint32_t sv = (ec >> 13) & 31;

    const int blockSize = 0x38;
    const int storedEnd = 0xE8;

    cryptArray(d.data() + 8, storedEnd - 8, ec);
    std::vector<uint8_t> tmp(d.begin() + 8, d.begin() + storedEnd);
    for (int i = 0; i < 4; i++) {
        int src = BLOCK_POS[(sv % 24) * 4 + i];
        memcpy(d.data() + 8 + i * blockSize, tmp.data() + src * blockSize, blockSize);
    }

    int species = rd16(d.data() + 8);
    r.species = species;
    r.name = (species > 0 && species < 1026) ? "PKM" : "???";
    r.level = -1;

    for (int i = 0; i < 13; i++) {
        uint32_t cu = rd16(d.data() + 0x40 + i * 2);
        if (cu == 0) break;
        appendUtf8(r.nickname, cu);
    }
    return r;
}

bool pk7DecryptSlot3DS(const uint8_t* slot, std::vector<uint8_t>& plain) {
    if (!pk7SlotPresent3DS(slot)) return false;
    plain.assign(slot, slot + 0xE8);
    uint32_t ec = rd32(plain.data());
    uint32_t sv = (ec >> 13) & 31;
    const int blockSize = 0x38;
    const int storedEnd = 0xE8;
    cryptArray(plain.data() + 8, storedEnd - 8, ec);
    std::vector<uint8_t> tmp(plain.begin() + 8, plain.begin() + storedEnd);
    for (int i = 0; i < 4; i++)
        memcpy(plain.data() + 8 + i * blockSize, tmp.data() + BLOCK_POS[(sv % 24) * 4 + i] * blockSize, blockSize);
    return true;
}

void pk7EncryptSlot3DS(std::vector<uint8_t>& plain) {
    if ((int)plain.size() != 0xE8) return;
    uint32_t ec = rd32(plain.data());
    uint32_t sv = (ec >> 13) & 31;
    const int blockSize = 0x38;
    const int storedEnd = 0xE8;
    uint16_t sum = 0;
    for (int i = 8; i < storedEnd; i += 2)
        sum = (uint16_t)(sum + rd16(plain.data() + i));
    plain[6] = (uint8_t)sum;
    plain[7] = (uint8_t)(sum >> 8);
    std::vector<uint8_t> tmp(plain.begin() + 8, plain.begin() + storedEnd);
    for (int i = 0; i < 4; i++)
        memcpy(plain.data() + 8 + BLOCK_POS[(sv % 24) * 4 + i] * blockSize, tmp.data() + i * blockSize, blockSize);
    cryptArray(plain.data() + 8, storedEnd - 8, ec);
}
