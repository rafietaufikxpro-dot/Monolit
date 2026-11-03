// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#include "sav.h"
#include "hash.h"
#include "pk.h"
#include "swish_consts.h"
#include <cstring>

namespace {

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// SCXorShift32 from PKHeX.
struct XS {
    uint32_t st;
    int c;
    static uint32_t adv(uint32_t s) { s ^= s << 2; s ^= s >> 15; s ^= s << 13; return s; }
    explicit XS(uint32_t seed) : st(seed), c(0) {
        int pop = __builtin_popcount(seed);
        for (int i = 0; i < pop; i++) st = adv(st);
    }
    XS(uint32_t state, int counter) : st(state), c(counter) {}
    uint8_t next() {
        uint8_t r = (uint8_t)(st >> (c << 3));
        if (c == 3) { st = adv(st); c = 0; } else { ++c; }
        return r;
    }
    uint32_t next32() {
        uint32_t b0 = next(), b1 = next(), b2 = next(), b3 = next();
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
};

// Static repeating xor layer. The pad is 0x7F long in effect (PKHeX advances by 0x7F per 0x80 chunk).
void staticXor(uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) d[i] ^= SWISH_XORPAD[i % 0x7F];
}

void swishHash(const uint8_t* data, size_t n, uint8_t out[32]) {
    Sha256 s;
    s.init();
    s.update(SWISH_INTRO, sizeof(SWISH_INTRO));
    s.update(data, n);
    s.update(SWISH_OUTRO, sizeof(SWISH_OUTRO));
    s.final(out);
}

int typeSize(int t) {
    switch (t) {
        case 3: case 8: case 12: return 1;
        case 9: case 13: return 2;
        case 10: case 14: case 16: return 4;
        case 11: case 15: case 17: return 8;
        default: return -1;
    }
}

struct Found {
    bool ok = false;
    size_t off = 0, len = 0;
    uint32_t st = 0;
    int c = 0;
    std::vector<std::pair<uint32_t, size_t>> idx;  // every block: (key, data length)
};

// Walks every block; fails if the structure does not end exactly at the payload end.
bool walkBlocks(const std::vector<uint8_t>& pl, uint32_t target, Found& f, std::string& err) {
    size_t off = 0, n = pl.size();
    while (off < n) {
        if (off + 5 > n) { err = "struktur blok save terpotong"; return false; }
        uint32_t key = rd32(&pl[off]);
        off += 4;
        XS xk(key);
        int type = pl[off++] ^ xk.next();
        size_t dataOff = 0, dataLen = 0;
        bool bytesOk = false;
        if (type >= 1 && type <= 3) {
            // no data
        } else if (type == 4) {
            if (off + 4 > n) { err = "blok objek terpotong"; return false; }
            int32_t len = (int32_t)(rd32(&pl[off]) ^ xk.next32());
            off += 4;
            if (len < 0 || off + (size_t)len > n) { err = "panjang blok tidak valid"; return false; }
            dataOff = off; dataLen = (size_t)len; off += dataLen; bytesOk = true;
        } else if (type == 5) {
            if (off + 5 > n) { err = "blok array terpotong"; return false; }
            int32_t cnt = (int32_t)(rd32(&pl[off]) ^ xk.next32());
            off += 4;
            int sub = pl[off++] ^ xk.next();
            int sz = typeSize(sub);
            if (cnt < 0 || sz < 0 || off + (size_t)cnt * sz > n) { err = "array blok tidak valid"; return false; }
            dataOff = off; dataLen = (size_t)cnt * sz; off += dataLen;
            bytesOk = (sub == 8);
        } else {
            int sz = typeSize(type);
            if (sz < 0 || off + sz > n) { err = "tipe blok tidak dikenal"; return false; }
            dataOff = off; dataLen = sz; off += sz;
        }
        f.idx.emplace_back(key, dataLen);
        if (key == target && !f.ok && bytesOk) {
            f.ok = true; f.off = dataOff; f.len = dataLen; f.st = xk.st; f.c = xk.c;
        }
    }
    return true;
}

const uint32_t BDSP_SIZES[4] = {0xE9828, 0xEDC20, 0xEED8C, 0xEF0A4};
const size_t BDSP_HASH_OFF = 0xE9828 - 16;

}  // namespace

bool SaveImage::load(const GameDef& g, std::vector<uint8_t> file, std::string& err) {
    game_ = nullptr;
    blocks_.clear();
    const FmtInfo* fi = fmtInfo(g.fmt);
    if (!fi) { err = "format tidak dikenal"; return false; }
    slotSize_ = fi->slotSize;
    size_t boxBytes = (size_t)g.boxes * g.perBox * slotSize_;
    file_ = std::move(file);

    if (g.family == FAM_SCBLOCK) {
        if (file_.size() < 64) { err = "file save terlalu kecil"; return false; }
        size_t n = file_.size() - 32;
        uint8_t h[32];
        swishHash(file_.data(), n, h);
        if (memcmp(h, file_.data() + n, 32) != 0) {
            err = "hash save tidak cocok (save rusak / versi game belum didukung)";
            return false;
        }
        payload_.assign(file_.begin(), file_.begin() + n);
        staticXor(payload_.data(), payload_.size());
        Found f;
        if (!walkBlocks(payload_, g.boxKey, f, err)) return false;
        if (!f.ok) { err = "blok box tidak ditemukan di save"; return false; }
        if (f.len < boxBytes) { err = "blok box lebih kecil dari yang diharapkan"; return false; }
        dataOff_ = f.off; xsState_ = f.st; xsCounter_ = f.c;
        blocks_ = std::move(f.idx);
        box_.resize(boxBytes);
        XS xs(xsState_, xsCounter_);
        for (size_t i = 0; i < boxBytes; i++) box_[i] = payload_[dataOff_ + i] ^ xs.next();
    } else {
        bool okSize = false;
        for (uint32_t s : BDSP_SIZES) if (file_.size() == s) okSize = true;
        if (!okSize) { err = "ukuran save BD/SP tidak dikenal"; return false; }
        std::vector<uint8_t> tmp = file_;
        uint8_t stored[16];
        memcpy(stored, &tmp[BDSP_HASH_OFF], 16);
        memset(&tmp[BDSP_HASH_OFF], 0, 16);
        uint8_t calc[16];
        md5(tmp.data(), tmp.size(), calc);
        if (memcmp(stored, calc, 16) != 0) { err = "MD5 save BD/SP tidak cocok (save rusak)"; return false; }
        if ((size_t)g.boxOffset + boxBytes > file_.size()) { err = "area box di luar file"; return false; }
        box_.assign(file_.begin() + g.boxOffset, file_.begin() + g.boxOffset + boxBytes);
    }
    game_ = &g;
    return true;
}

const uint8_t* SaveImage::slot(int box, int idx) const {
    return &box_[((size_t)box * game_->perBox + idx) * slotSize_];
}

bool SaveImage::present(int box, int idx) const { return pkSlotPresent(slot(box, idx)); }

bool SaveImage::put(int box, int idx, const uint8_t* data, size_t len) {
    if (!game_ || len != (size_t)slotSize_) return false;
    memcpy(&box_[((size_t)box * game_->perBox + idx) * slotSize_], data, len);
    return true;
}

void SaveImage::clear(int box, int idx) {
    memset(&box_[((size_t)box * game_->perBox + idx) * slotSize_], 0, slotSize_);
}

bool SaveImage::build(std::vector<uint8_t>& out, std::string& err) const {
    if (!game_) { err = "save belum dimuat"; return false; }
    if (game_->family == FAM_SCBLOCK) {
        std::vector<uint8_t> pl = payload_;
        XS xs(xsState_, xsCounter_);
        for (size_t i = 0; i < box_.size(); i++) pl[dataOff_ + i] = box_[i] ^ xs.next();
        staticXor(pl.data(), pl.size());
        out = pl;
        out.resize(pl.size() + 32);
        swishHash(pl.data(), pl.size(), &out[pl.size()]);
    } else {
        out = file_;
        memcpy(&out[game_->boxOffset], box_.data(), box_.size());
        memset(&out[BDSP_HASH_OFF], 0, 16);
        uint8_t h[16];
        md5(out.data(), out.size(), h);
        memcpy(&out[BDSP_HASH_OFF], h, 16);
    }
    return true;
}

bool SaveImage::hasBlock(uint32_t key) const {
    for (const auto& b : blocks_)
        if (b.first == key && b.second > 0) return true;
    return false;
}

// Same rules as PKHeX: SAV8SWSH (Zukan8.GetRevision) and SAV9SV (SaveRevision).
int SaveImage::dlcLevel() const {
    if (!game_) return 0;
    switch (game_->fmt) {
        case FMT_PK8:  // Sword/Shield: Zukan blocks for Isle of Armor (R1) and Crown Tundra (R2)
            if (!hasBlock(0x3F936BA9)) return 0;
            if (!hasBlock(0x3C9366F0)) return 1;
            return 2;
        case FMT_PK9:  // Scarlet/Violet: Blueberry points block => 2, Kitakami raid block => 1
            if (hasBlock(0x66A33824)) return 2;
            if (hasBlock(0x100B93DA)) return 1;
            return 0;
        default:
            return 0;
    }
}

const char* dlcName(Fmt fmt, int level) {
    if (fmt == FMT_PK8) return level == 1 ? "DLC Isle of Armor" : level >= 2 ? "DLC Crown Tundra" : "";
    if (fmt == FMT_PK9) return level == 1 ? "DLC Teal Mask" : level >= 2 ? "DLC Indigo Disk" : "";
    return "";
}
