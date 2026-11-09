// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#include "sidedata.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "hash.h"
#include "pk.h"

namespace {
const char MAGIC[8] = {'M', 'O', 'N', 'O', 'S', 'I', 'D', '1'};

void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool readAll(const std::string& path, std::vector<uint8_t>& out, bool& missing) {
    missing = false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { missing = (errno == ENOENT); return false; }
    out.clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    bool bad = ferror(f) != 0;
    fclose(f);
    return !bad;
}

std::vector<uint8_t> serialize(const std::vector<SideEntry>& entries) {
    std::vector<uint8_t> out(MAGIC, MAGIC + 8);
    put32(out, (uint32_t)entries.size());
    for (const SideEntry& e : entries) {
        out.push_back((uint8_t)e.fmt);
        put32(out, e.key.ec); put32(out, e.key.pid); put32(out, e.key.tidsid);
        out.push_back((uint8_t)(e.slot.size() & 0xFF));
        out.push_back((uint8_t)(e.slot.size() >> 8));
        out.insert(out.end(), e.slot.begin(), e.slot.end());
    }
    Sha256 h;
    h.init();
    h.update(out.data(), out.size());
    uint8_t d[32];
    h.final(d);
    out.insert(out.end(), d, d + 32);
    return out;
}

bool parse(const std::vector<uint8_t>& b, std::vector<SideEntry>& out, std::string& err) {
    out.clear();
    if (b.size() < 8 + 4 + 32 || memcmp(b.data(), MAGIC, 8) != 0) { err = "side data file has an unknown format"; return false; }
    Sha256 h;
    h.init();
    h.update(b.data(), b.size() - 32);
    uint8_t d[32];
    h.final(d);
    if (memcmp(d, &b[b.size() - 32], 32) != 0) { err = "side data file failed its integrity check"; return false; }
    const size_t end = b.size() - 32;
    size_t off = 8;
    uint32_t count = get32(&b[off]);
    off += 4;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 15 > end) { err = "side data file is truncated"; return false; }
        SideEntry e;
        e.fmt = (Fmt)b[off++];
        e.key.ec = get32(&b[off]); e.key.pid = get32(&b[off + 4]); e.key.tidsid = get32(&b[off + 8]);
        off += 12;
        size_t sz = b[off] | (b[off + 1] << 8);
        off += 2;
        const FmtInfo* fi = fmtInfo(e.fmt);
        if (!fi || sz != (size_t)fi->slotSize || off + sz > end) { err = "invalid side data entry"; return false; }
        e.slot.assign(b.begin() + off, b.begin() + off + sz);
        off += sz;
        out.push_back(std::move(e));
    }
    if (off != end) { err = "side data file has trailing bytes"; return false; }
    return true;
}
}  // namespace

bool sideKeyOf(Fmt fmt, const uint8_t* slot, SideKey& key) {
    std::vector<uint8_t> p;
    if (!pkDecryptSlot(fmt, slot, p)) return false;
    key.ec = get32(&p[0]);
    key.pid = get32(&p[0x1C]);
    key.tidsid = get32(&p[0x0C]);
    return true;
}

bool SideStore::load(const std::string& path, std::string& err) {
    entries.clear();
    recovered_ = false;
    const std::string bak = path + ".bak";
    std::vector<uint8_t> b;
    bool missing = false;
    std::string mainErr;

    if (!readAll(path, b, missing)) {
        if (!missing) { err = "cannot read " + path; return false; }  // I/O error: never guess
    } else if (parse(b, entries, mainErr)) {
        return true;
    }
    // The main file is missing or damaged: try the backup before giving up.
    std::vector<uint8_t> bb;
    bool bakMissing = false;
    if (!readAll(bak, bb, bakMissing)) {
        if (bakMissing) {
            if (missing) return true;  // first run: empty store
            err = mainErr;             // damaged main file and no backup
            return false;
        }
        err = "cannot read " + bak;
        return false;
    }
    std::string bakErr;
    if (!parse(bb, entries, bakErr)) {
        entries.clear();
        err = (missing ? std::string("side data file missing and backup damaged: ") : mainErr + "; backup also damaged: ") + bakErr;
        return false;
    }
    recovered_ = true;
    return true;
}

bool SideStore::save(const std::string& path, std::string& err) const {
    const std::vector<uint8_t> out = serialize(entries);
    const std::string tmp = path + ".tmp", bak = path + ".bak";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) { err = "cannot write side data file"; return false; }
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (fclose(f) == 0) && ok;
    if (ok) {
        std::vector<uint8_t> back;
        bool missing;
        ok = readAll(tmp, back, missing) && back == out;
    }
    if (!ok) { remove(tmp.c_str()); err = "failed to write side data file (SD card full?)"; return false; }

    // swap it in
    if (recovered_) {
        // The main file is missing/invalid and ".bak" is the last good copy: keep ".bak" as it is.
        remove(path.c_str());
        if (rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); err = "failed to replace side data file"; return false; }
        recovered_ = false;
        return true;
    }
    remove(bak.c_str());  // safe: the new file is already complete on disk
    bool hadMain = rename(path.c_str(), bak.c_str()) == 0;
    if (!hadMain && errno != ENOENT) { remove(tmp.c_str()); err = "failed to back up side data file"; return false; }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        if (hadMain) rename(bak.c_str(), path.c_str());
        remove(tmp.c_str());
        err = "failed to replace side data file";
        return false;
    }
    return true;
}

void SideStore::put(Fmt fmt, const uint8_t* slot) {
    const FmtInfo* fi = fmtInfo(fmt);
    SideKey k;
    if (!fi || !sideKeyOf(fmt, slot, k)) return;
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].fmt == fmt && entries[i].key == k) { entries.erase(entries.begin() + i); break; }
    }
    SideEntry e;
    e.fmt = fmt;
    e.key = k;
    e.slot.assign(slot, slot + fi->slotSize);
    entries.push_back(std::move(e));
    while (entries.size() > MAX_ENTRIES) entries.erase(entries.begin());  // drop the oldest
}

const SideEntry* SideStore::find(Fmt fmt, const SideKey& key) const {
    for (const SideEntry& e : entries)
        if (e.fmt == fmt && e.key == key) return &e;
    return nullptr;
}
