// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
#include "bank.h"
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {

void w32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }

enum ReadResult { READ_OK, READ_MISSING, READ_ERROR };

ReadResult readFile(const std::string& path, std::vector<uint8_t>& buf, std::string& err) {
    buf.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (errno == ENOENT) return READ_MISSING;
        err = "cannot open " + path;
        return READ_ERROR;
    }
    uint8_t tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + n);
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) { err = "failed to read " + path; buf.clear(); return READ_ERROR; }
    return READ_OK;
}

bool parse(const std::vector<uint8_t>& buf, std::vector<BankEntry>& out, std::string& err) {
    out.clear();
    if (buf.size() < 16 || memcmp(buf.data(), "PKBANK01", 8) != 0) { err = "bank file is not in PKBANK01 format"; return false; }
    uint32_t count = buf[8] | (buf[9] << 8) | (buf[10] << 16) | ((uint32_t)buf[11] << 24);
    size_t off = 16;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 4 > buf.size()) { err = "bank file is truncated"; out.clear(); return false; }
        BankEntry e;
        e.fmt = (Fmt)buf[off];
        e.dlc = buf[off + 1];
        uint16_t sz = (uint16_t)(buf[off + 2] | (buf[off + 3] << 8));
        off += 4;
        const FmtInfo* fi = fmtInfo(e.fmt);
        if (!fi || sz != fi->slotSize || off + sz > buf.size()) { err = "invalid bank entry"; out.clear(); return false; }
        e.data.assign(buf.begin() + off, buf.begin() + off + sz);
        off += sz;
        out.push_back(std::move(e));
    }
    return true;
}

}  // namespace

bool Bank::load(const std::string& path, std::string& err) {
    entries.clear();
    recovered_ = false;
    std::string bak = path + ".bak";
    std::vector<uint8_t> buf;
    std::string mainErr;

    ReadResult r = readFile(path, buf, mainErr);
    if (r == READ_ERROR) { err = mainErr; return false; }  // never guess: do not fall back after an I/O error
    if (r == READ_OK) {
        if (parse(buf, entries, mainErr)) return true;
        // main file is damaged: try the backup below
    }

    std::string bakErr;
    ReadResult rb = readFile(bak, buf, bakErr);
    if (rb == READ_MISSING) {
        if (r == READ_MISSING) return true;  // first run: empty bank
        err = mainErr;                       // damaged main file and no backup
        return false;
    }
    if (rb == READ_ERROR) { err = bakErr; return false; }
    std::string parseErr;
    if (!parse(buf, entries, parseErr)) {
        entries.clear();
        err = (r == READ_MISSING ? std::string("bank missing and backup damaged: ") : mainErr + "; backup also damaged: ") + parseErr;
        return false;
    }
    recovered_ = true;
    return true;
}

bool Bank::save(const std::string& path, std::string& err) const {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'P', 'K', 'B', 'A', 'N', 'K', '0', '1'});
    w32(out, (uint32_t)entries.size());
    w32(out, 0);
    for (const auto& e : entries) {
        out.push_back((uint8_t)e.fmt);
        out.push_back(e.dlc);
        out.push_back((uint8_t)(e.data.size() & 0xFF));
        out.push_back((uint8_t)(e.data.size() >> 8));
        out.insert(out.end(), e.data.begin(), e.data.end());
    }

    // 1. write the new file next to the real one and read it back before touching anything
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) { err = "cannot write bank file"; return false; }
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (fclose(f) == 0) && ok;
    if (ok) {
        std::vector<uint8_t> back;
        std::string e2;
        ok = readFile(tmp, back, e2) == READ_OK && back == out;
    }
    if (!ok) { remove(tmp.c_str()); err = "failed to write bank file (SD card full?)"; return false; }

    // 2. swap it in
    std::string bak = path + ".bak";
    if (recovered_) {
        // The main file is missing/invalid and ".bak" is the last good copy: keep ".bak" as it is.
        remove(path.c_str());
        if (rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); err = "failed to replace bank file"; return false; }
        recovered_ = false;
        return true;
    }
    remove(bak.c_str());  // safe: the new file is already complete on disk
    bool hadMain = rename(path.c_str(), bak.c_str()) == 0;
    if (!hadMain && errno != ENOENT) { remove(tmp.c_str()); err = "failed to create bank backup"; return false; }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        if (hadMain) rename(bak.c_str(), path.c_str());
        remove(tmp.c_str());
        err = "failed to replace bank file";
        return false;
    }
    return true;
}
