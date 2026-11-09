// Monolit 3DS - SaveImage7 (Gen 7 save)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#include "sav7.h"
#include "pk7.h"
#include "hash.h"
#include <cstring>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool SaveImage7::load(const GameDef& g, std::vector<uint8_t> file, std::string& err) {
    game_ = nullptr;
    slotSize_ = 0xE8;
    size_t boxBytes = (size_t)g.boxes * g.perBox * slotSize_;
    file_ = std::move(file);

    if (g.saveSize != 0 && file_.size() != g.saveSize) {
        err = "save size mismatch";
        return false;
    }
    if (g.boxOffset + boxBytes > file_.size()) {
        err = "box area outside the save file";
        return false;
    }

    box_.assign(file_.begin() + g.boxOffset, file_.begin() + g.boxOffset + boxBytes);

    // Verify crc16 checksums per block
    if (g.chkofs && g.chklen && g.chkCount > 0) {
        for (int i = 0; i < g.chkCount; i++) {
            uint32_t off = g.chkofs[i];
            uint32_t len = g.chklen[i];
            if (off + len > file_.size()) {
                err = "checksum block outside save file";
                return false;
            }
            if (len < 2) continue;
            uint16_t stored = rd16(&file_[off + len - 2]);
            uint16_t actual = crc16(&file_[off], len - 2);
            if (stored != actual) {
                err = "checksum mismatch at block " + std::to_string(i);
                return false;
            }
        }
    }

    // Decrypt each box slot
    for (int b = 0; b < g.boxes; b++) {
        for (int s = 0; s < g.perBox; s++) {
            uint8_t* slot = &box_[(size_t)(b * g.perBox + s) * slotSize_];
            std::vector<uint8_t> plain;
            if (!pk7DecryptSlot3DS(slot, plain)) continue;  // empty slot, leave as-is
            memcpy(slot, plain.data(), slotSize_);
        }
    }

    game_ = &g;
    return true;
}

const uint8_t* SaveImage7::slot(int box, int idx) const {
    return &box_[((size_t)box * game_->perBox + idx) * slotSize_];
}

bool SaveImage7::present(int box, int idx) const {
    const uint8_t* s = slot(box, idx);
    uint32_t ec = s[0] | (s[1] << 8) | (s[2] << 16) | ((uint32_t)s[3] << 24);
    uint16_t species = s[8] | (s[9] << 8);
    return ec != 0 || species != 0;
}

bool SaveImage7::put(int box, int idx, const uint8_t* data, size_t len) {
    if (!game_ || len != (size_t)slotSize_) return false;
    // data is plaintext; encrypt before storing
    std::vector<uint8_t> enc(data, data + len);
    pk7EncryptSlot3DS(enc);
    memcpy(&box_[((size_t)box * game_->perBox + idx) * slotSize_], enc.data(), len);
    return true;
}

void SaveImage7::clear(int box, int idx) {
    memset(&box_[((size_t)box * game_->perBox + idx) * slotSize_], 0, slotSize_);
}

bool SaveImage7::build(std::vector<uint8_t>& out, std::string& err) const {
    if (!game_) { err = "save not loaded"; return false; }
    out = file_;

    // Re-encrypt all box slots (they are stored plaintext in box_)
    for (int b = 0; b < game_->boxes; b++) {
        for (int s = 0; s < game_->perBox; s++) {
            uint8_t* slot = &box_[(size_t)(b * game_->perBox + s) * slotSize_];
            std::vector<uint8_t> plain(slot, slot + slotSize_);
            // Skip empty slots
            bool empty = true;
            for (size_t i = 0; i < slotSize_; i++) { if (plain[i] != 0) { empty = false; break; } }
            if (empty) continue;
            pk7EncryptSlot3DS(plain);
            memcpy(slot, plain.data(), slotSize_);
        }
    }

    // Copy encrypted box area back to file
    memcpy(&out[game_->boxOffset], box_.data(), box_.size());

    // Recompute crc16 checksums per block
    if (game_->chkofs && game_->chklen && game_->chkCount > 0) {
        for (int i = 0; i < game_->chkCount; i++) {
            uint32_t off = game_->chkofs[i];
            uint32_t len = game_->chklen[i];
            if (off + len > out.size()) continue;
            if (len < 2) continue;
            uint16_t chk = crc16(&out[off], len - 2);
            out[off + len - 2] = (uint8_t)(chk & 0xFF);
            out[off + len - 1] = (uint8_t)(chk >> 8);
        }
    }

    return true;
}
