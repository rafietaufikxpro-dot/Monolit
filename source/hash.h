// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
// SHA-256 and MD5, self-contained so the core builds on both Switch and host.
#pragma once
#include <cstddef>
#include <cstdint>

struct Sha256 {
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[64];
    size_t n;
    void init();
    void update(const uint8_t* data, size_t len);
    void final(uint8_t out[32]);
};

void md5(const uint8_t* data, size_t len, uint8_t out[16]);

uint16_t ccitt16(const uint8_t* data, size_t len);
uint16_t crc16(const uint8_t* data, size_t len);
