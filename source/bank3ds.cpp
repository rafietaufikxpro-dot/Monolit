// Monolit 3DS - bank file MONO3D01 (3DS)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#include "bank3ds.h"
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

bool parse(const std::vector<uint8_t>& buf, std::vector<Bank3DSEntry>& out, std::string& err) {
    out.clear();
    if (buf.size() < 16 || memcmp(buf.data(), "MONO3D01", 8) != 0) { err = "bank file is not in MONO3D01 format"; return false; }
    uint32_t count = buf[8] | (buf[9] << 8) | (buf[10] << 16) | ((uint32_t)buf[11] << 24);
    size_t off = 16;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 3 > buf.size()) { err = "bank file is truncated"; out.clear(); return false; }
        Bank3DSEntry e;
        e.fmt = (Fmt)buf[off];
        uint16_t sz = (uint16_t)(buf[off + 1] | (buf[off + 2] << 8));
        off += 3;
        off += sz;
        out.push_back(std::move(e));
    }
    return true;
}

}  // namespace

bool Bank3DS::load(const std::string& path, std::string& err) {
    entries.clear();
    recovered_ = false;
    std::string bak = path + ".bak";
    std::vector<uint8_t> buf;
    std::string mainErr;

    ReadResult r = readFile(path, buf, mainErr);
    if (r == READ_ERROR) { err = mainErr; return false; }
    if (r == READ_OK) {
        if (parse(buf, entries, mainErr)) return true;
    }

    ReadResult rb = readFile(bak, buf, mainErr);
    if (rb == READ_MISSING) {
        if (r == READ_MISSING) return true;
        err = mainErr;
        return false;
    }
    if (rb == READ_ERROR) { err = mainErr; return false; }
    if (!parse(buf, entries, mainErr)) {
        entries.clear();
        err = "backup also damaged: " + mainErr;
        return false;
    }
    recovered_ = true;
    return true;
}

bool Bank3DS::save(const std::string& path, std::string& err) const {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'M', 'O', 'N', 'O', '3', 'D', '0', '1'});
    w32(out, (uint32_t)entries.size());
    w32(out, 0);
    for (const auto& e : entries) {
        out.push_back((uint8_t)e.fmt);
        out.push_back((uint8_t)(e.data.size() & 0xFF));
        out.push_back((uint8_t)(e.data.size() >> 8));
        out.insert(out.end(), e.data.begin(), e.data.end());
    }

    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) { err = "cannot write bank file"; return false; }
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp.c_str()); err = "failed to write bank file"; return false; }

    std::string bak = path + ".bak";
    if (recovered_) {
        remove(path.c_str());
        if (rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); err = "failed to replace bank file"; return false; }
        recovered_ = false;
        return true;
    }
    remove(bak.c_str());
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
