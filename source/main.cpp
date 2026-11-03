// Monolit - offline Pokemon bank for Switch CFW
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 3 as published by the Free Software Foundation.
// It is distributed WITHOUT ANY WARRANTY. See the LICENSE file. Not affiliated with Nintendo or The Pokemon Company.
// Monolit - offline Pokemon bank for Switch CFW (homebrew, SDL2 UI).
// Core logic (sav/bank/pk/hash) is platform independent and unit tested on a PC.
// This file = Switch-only glue (libnx: accounts, save mount, pad). Drawing lives in ui.cpp (SDL2).
#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "bank.h"
#include "convert.h"
#include "games.h"
#include "pk.h"
#include "sav.h"
#include "ui.h"

static const char* DIR_ROOT = "sdmc:/switch/Monolit";
static const char* DIR_BACKUP = "sdmc:/switch/Monolit/backups";
static const char* BANK_FILE = "sdmc:/switch/Monolit/monolit.bank";

static const char* APP_VERSION = "0.2.0";
static const int KEEP_BACKUPS = 10;  // newest backups kept per game

static PadState g_pad;
static AccountUid g_uid;
static Bank g_bank;

using DrawFn = std::function<void()>;
static const DrawFn NO_BASE = [] {};

// ---------------------------------------------------------------- input / dialogs
// Redraws every frame until a button is pressed (so the screen also recovers after the HOME menu).
static u64 waitKey(const DrawFn& draw) {
    while (appletMainLoop()) {
        draw();
        ui::present();
        padUpdate(&g_pad);
        u64 d = padGetButtonsDown(&g_pad);
        if (d) return d;
    }
    return HidNpadButton_Plus;  // applet is closing
}

static void notice(const DrawFn& base, const std::string& title, const std::string& body, bool warn = false) {
    waitKey([&] { ui::begin(); base(); ui::dialog(title, body, {{"A", "OK"}}, warn ? ui::WARN : ui::ACCENT); });
}

// Returns true only if A was pressed (any other button = no).
static bool confirmA(const DrawFn& base, const std::string& title, const std::string& body, const char* yes, const char* no, bool warn) {
    u64 k = waitKey([&] { ui::begin(); base(); ui::dialog(title, body, {{"A", yes}, {"B", no}}, warn ? ui::WARN : ui::ACCENT); });
    return (k & HidNpadButton_A) != 0;
}

static void busy(const DrawFn& base, const std::string& title, const std::string& body) {
    ui::begin();
    base();
    ui::dialog(title, body, {});
    ui::present();
}

static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t tmp[16384];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) out.insert(out.end(), tmp, tmp + n);
    fclose(f);
    return !out.empty();
}

static bool mountSave(u64 tid) {
    FsFileSystem fs;
    if (R_FAILED(fsOpen_SaveData(&fs, tid, g_uid))) return false;
    if (fsdevMountDevice("save", fs) < 0) { fsFsClose(&fs); return false; }
    return true;
}
static void unmountSave() { fsdevUnmountDevice("save"); }

static std::string hex64(u64 v) { char b[20]; snprintf(b, sizeof(b), "%016llX", (unsigned long long)v); return b; }

// The app used to be called "PokeBank" (data in sdmc:/switch/PokeBank, bank file pokebank.bank). On first start after the
// rename, move that data over so existing banks and backups are kept. Nothing is deleted; on failure the old data stays put.
static bool g_migrationFailed = false;
static bool pathExists(const char* p) { struct stat st; return stat(p, &st) == 0; }
static void migrateOldData() {
    const char* oldRoot = "sdmc:/switch/PokeBank";
    if (!pathExists(oldRoot) || pathExists(DIR_ROOT)) return;
    if (rename(oldRoot, DIR_ROOT) != 0) { g_migrationFailed = true; return; }
    const std::string base = std::string(DIR_ROOT) + "/";
    for (const char* suffix : {"", ".bak"}) {
        const std::string from = base + "pokebank.bank" + suffix, to = std::string(BANK_FILE) + suffix;
        if (pathExists(from.c_str()) && !pathExists(to.c_str())) rename(from.c_str(), to.c_str());
    }
}

static void ensureDirs() {
    migrateOldData();
    mkdir(DIR_ROOT, 0777);
    mkdir(DIR_BACKUP, 0777);
}

// Deletes the oldest backups of one game, keeping the newest `keep`.
// File names are "<titleid>_<unix time>_<save file>.bak"; order is decided by the numeric time.
static void pruneBackups(u64 titleId, int keep) {
    DIR* d = opendir(DIR_BACKUP);
    if (!d) return;
    const std::string prefix = hex64(titleId) + "_";
    std::vector<std::pair<long long, std::string>> files;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() <= prefix.size() + 4 || n.compare(0, prefix.size(), prefix) != 0 || n.compare(n.size() - 4, 4, ".bak") != 0) continue;
        files.emplace_back(strtoll(n.c_str() + prefix.size(), NULL, 10), n);
    }
    closedir(d);
    if ((int)files.size() <= keep) return;
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i + keep < files.size(); i++) remove((std::string(DIR_BACKUP) + "/" + files[i].second).c_str());
}

// ---------------------------------------------------------------- users
static std::vector<AccountUid> g_users;

static bool sameUid(const AccountUid& a, const AccountUid& b) { return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1]; }

static void loadUsers() {
    g_users.clear();
    AccountUid list[ACC_USER_LIST_SIZE];
    s32 total = 0;
    if (R_SUCCEEDED(accountListAllUsers(list, ACC_USER_LIST_SIZE, &total)))
        for (s32 i = 0; i < total && i < ACC_USER_LIST_SIZE; i++) g_users.push_back(list[i]);
}

static std::string userName(const AccountUid& uid) {
    AccountProfile prof;
    if (R_SUCCEEDED(accountGetProfile(&prof, uid))) {
        AccountProfileBase base;
        memset(&base, 0, sizeof(base));
        bool ok = R_SUCCEEDED(accountProfileGet(&prof, NULL, &base));
        accountProfileClose(&prof);
        size_t n = strnlen(base.nickname, sizeof(base.nickname));
        if (ok && n > 0) return std::string(base.nickname, n);
    }
    char b[24];
    snprintf(b, sizeof(b), "%016llX", (unsigned long long)uid.uid[0]);
    return b;
}

// Lets the player choose which Switch user's saves are used. Returns true if g_uid was set.
// With allowCancel == false the player must choose (used at startup when the user is ambiguous).
static bool screenPickUser(bool allowCancel) {
    loadUsers();
    if (g_users.empty()) return false;
    std::vector<std::string> names;
    int cur = 0, top = 0;
    for (size_t i = 0; i < g_users.size(); i++) {
        names.push_back(userName(g_users[i]));
        if (sameUid(g_users[i], g_uid)) cur = (int)i;
    }
    int n = (int)g_users.size();
    auto draw = [&] {
        ui::begin();
        ui::topBar("Pilih user", "Save dibaca per user Switch");
        if (cur < top) top = cur;
        if (cur >= top + ui::LIST_ROWS) top = cur - ui::LIST_ROWS + 1;
        for (int i = top; i < n && i < top + ui::LIST_ROWS; i++) ui::row(i - top, i == cur, std::to_string(i + 1) + "   " + names[i], "");
        std::vector<ui::Hint> h{{"A", "Pakai user ini"}};
        if (allowCancel) h.push_back({"B", "Batal"});
        ui::footBar(h);
    };
    while (appletMainLoop()) {
        u64 k = waitKey(draw);
        if ((k & HidNpadButton_Down) && cur + 1 < n) cur++;
        if ((k & HidNpadButton_Up) && cur > 0) cur--;
        if (k & HidNpadButton_A) { g_uid = g_users[cur]; return true; }
        if (allowCancel && (k & HidNpadButton_B)) return false;
    }
    return false;
}

static std::string dlcTag(Fmt fmt, int need) {
    const char* n = need > 0 ? dlcName(fmt, need) : "";
    return *n ? std::string(" <") + n + ">" : "";
}

static std::string describe(Fmt fmt, const uint8_t* slot) {
    PkSummary s = pkSummarize(fmt, slot);
    if (!s.present) return "-";
    std::string r = s.name;
    if (s.level >= 0) r += "   Lv." + std::to_string(s.level);
    if (!s.nickname.empty() && s.nickname != s.name) r += "   \"" + s.nickname + "\"";
    return r;
}

// ---------------------------------------------------------------- bank browser
static void screenBank() {
    int cur = 0, top = 0;
    std::string msg;
    bool confirmDel = false;
    auto base = [&] {
        int n = (int)g_bank.entries.size();
        ui::begin();
        ui::topBar("Bank", std::to_string(n) + " Pokémon" + (n ? "   (" + std::to_string(cur + 1) + "/" + std::to_string(n) + ")" : ""));
        if (cur < top) top = cur;
        if (cur >= top + ui::LIST_ROWS) top = cur - ui::LIST_ROWS + 1;
        for (int i = top; i < n && i < top + ui::LIST_ROWS; i++) {
            const BankEntry& e = g_bank.entries[i];
            const FmtInfo* fi = fmtInfo(e.fmt);
            ui::row(i - top, i == cur, std::to_string(i + 1) + "   " + describe(e.fmt, e.data.data()),
                    std::string(fi ? fi->name : "?") + dlcTag(e.fmt, bankDlcNeeded(e)));
        }
        if (n == 0) ui::text(ui::W / 2, 300, "Bank kosong", 28, ui::DIM, 1);
        ui::footBar({{"Atas/Bawah", "Pilih"}, {"Kiri/Kanan", "±10"}, {"X", "Hapus dari bank"}, {"B", "Kembali"}});
        ui::toast(msg);
    };
    while (appletMainLoop()) {
        u64 k;
        if (confirmDel) {
            k = waitKey([&] { base(); ui::dialog("Hapus permanen?", "Entri ini akan dihapus PERMANEN dari bank dan tidak bisa dikembalikan.", {{"X", "Hapus"}, {"B", "Batal"}}, ui::BAD); });
        } else {
            k = waitKey(base);
        }
        int n = (int)g_bank.entries.size();
        if (confirmDel) {
            confirmDel = false;
            if ((k & HidNpadButton_X) && n > 0) {
                g_bank.entries.erase(g_bank.entries.begin() + cur);
                std::string err;
                msg = g_bank.save(BANK_FILE, err) ? "Entri dihapus." : ("GAGAL simpan bank: " + err);
                if (cur >= (int)g_bank.entries.size() && cur > 0) cur--;
            }
            continue;
        }
        msg.clear();
        n = (int)g_bank.entries.size();
        if (k & HidNpadButton_B) return;
        if ((k & HidNpadButton_Down) && cur + 1 < n) cur++;
        if ((k & HidNpadButton_Up) && cur > 0) cur--;
        if ((k & HidNpadButton_Right) && n) cur = (cur + 10 < n) ? cur + 10 : n - 1;
        if ((k & HidNpadButton_Left)) cur = (cur >= 10) ? cur - 10 : 0;
        if ((k & HidNpadButton_X) && n > 0) confirmDel = true;
    }
}

// ---------------------------------------------------------------- withdraw picker
struct PickResult {
    int index = -1;                 // index into g_bank.entries, -1 = cancelled
    std::vector<uint8_t> data;      // bytes to write into the save (converted if `converted`)
    bool converted = false;
    Fmt from = FMT_NONE;
};

// Lists bank entries of this format plus convertible ones (PK8 <-> PB8). Converted data is computed once up front.
static PickResult pickFromBank(Fmt fmt, const std::vector<bool>& taken, int haveDlc) {
    struct Row { int bank; conv::Result res; std::vector<uint8_t> data; };
    std::vector<Row> rows;
    for (size_t i = 0; i < g_bank.entries.size(); i++) {
        const BankEntry& e = g_bank.entries[i];
        if (taken[i]) continue;
        if (e.fmt == fmt) { rows.push_back({(int)i, conv::OK, {}}); continue; }
        if (!conv::supported(e.fmt, fmt)) continue;
        Row r{(int)i, conv::OK, {}};
        r.res = conv::convertSlot(e.fmt, fmt, e.data.data(), r.data);
        rows.push_back(std::move(r));
    }
    int cur = 0, top = 0;
    int n = (int)rows.size();
    auto draw = [&] {
        ui::begin();
        ui::topBar("Ambil dari bank", std::to_string(n) + " bisa dipakai di game ini");
        if (cur < top) top = cur;
        if (cur >= top + ui::LIST_ROWS) top = cur - ui::LIST_ROWS + 1;
        for (int i = top; i < n && i < top + ui::LIST_ROWS; i++) {
            const BankEntry& be = g_bank.entries[rows[i].bank];
            std::string right;
            ui::Col col = ui::WARN;
            if (be.fmt != fmt) {
                const FmtInfo* sf = fmtInfo(be.fmt);
                if (rows[i].res == conv::OK) right = std::string("KONVERSI dari ") + (sf ? sf->name : "?");
                else { right = "TIDAK BISA: " + std::string(conv::describe(rows[i].res)); col = ui::BAD; }
            } else if (bankDlcNeeded(be) > haveDlc) {
                right = std::string("BUTUH ") + dlcName(fmt, bankDlcNeeded(be));
            }
            ui::row(i - top, i == cur, std::to_string(i + 1) + "   " + describe(be.fmt, be.data.data()), right, col);
        }
        if (n == 0) ui::text(ui::W / 2, 300, "Tidak ada Pokémon di bank yang cocok untuk game ini", 26, ui::DIM, 1);
        else ui::text(40, ui::H - 90, "KONVERSI = simulasi HOME offline: move di-reset ke learnset game tujuan; hanya untuk dipakai offline.", 18, ui::DIM);
        ui::footBar({{"Atas/Bawah", "Pilih"}, {"A", "Ambil"}, {"B", "Batal"}});
    };
    while (appletMainLoop()) {
        u64 k = waitKey(draw);
        if (k & HidNpadButton_B) return {};
        if ((k & HidNpadButton_A) && n) {
            const BankEntry& be = g_bank.entries[rows[cur].bank];
            PickResult pr;
            pr.index = rows[cur].bank;
            pr.from = be.fmt;
            if (be.fmt == fmt) { pr.data = be.data; return pr; }
            if (rows[cur].res != conv::OK) { notice(draw, "Tidak bisa dikonversi", conv::describe(rows[cur].res), true); continue; }
            const FmtInfo* sf = fmtInfo(be.fmt);
            const FmtInfo* df = fmtInfo(fmt);
            std::string body = std::string("Dari ") + (sf ? sf->name : "?") + " ke " + (df ? df->name : "?") + ".\n"
                "Move di-reset ke yang diberikan game tujuan pada level ini, ability/ball/lokasi disesuaikan, held item dihapus. Move asli tidak bisa dikembalikan setelah ditulis.\n"
                "Hasil konversi tidak punya tracker HOME yang sah: pakai HANYA offline (jangan ke HOME asli, trade, atau battle online).";
            if (fmt == FMT_PK8) body += "\nJika Pokémon ini hanya ada di DLC Sword/Shield, pastikan save tujuan punya DLC-nya.";
            if (!confirmA(draw, "Konversi antar game", body, "Lanjut konversi", "Batal", true)) continue;
            pr.data = rows[cur].data;
            pr.converted = true;
            return pr;
        }
        if ((k & HidNpadButton_Down) && cur + 1 < n) cur++;
        if ((k & HidNpadButton_Up) && cur > 0) cur--;
        if ((k & HidNpadButton_Right) && n) cur = (cur + 10 < n) ? cur + 10 : n - 1;
        if (k & HidNpadButton_Left) cur = (cur >= 10) ? cur - 10 : 0;
    }
    return {};
}

// ---------------------------------------------------------------- commit to save
static bool commitSave(const GameDef& g, SaveImage& img, std::vector<uint8_t>& orig, std::string& err) {
    std::vector<uint8_t> out;
    if (!img.build(out, err)) return false;

    // 1. verify what we are about to write is itself a valid save
    {
        SaveImage check;
        if (!check.load(g, out, err)) { err = "verifikasi sebelum tulis gagal: " + err; return false; }
    }

    // 2. backup original save to SD (never skipped)
    char name[160];
    snprintf(name, sizeof(name), "%s/%s_%lld_%s.bak", DIR_BACKUP, hex64(g.titleId).c_str(), (long long)time(NULL), g.saveFile);
    FILE* bf = fopen(name, "wb");
    if (!bf) { err = "tidak bisa membuat backup, penulisan dibatalkan"; return false; }
    bool bok = fwrite(orig.data(), 1, orig.size(), bf) == orig.size();
    bok = (fclose(bf) == 0) && bok;
    if (!bok) { err = "backup gagal ditulis, penulisan dibatalkan"; return false; }

    // 3. write + commit
    std::string path = std::string("save:/") + g.saveFile;
    FILE* f = fopen(path.c_str(), "r+b");
    if (!f) { err = "tidak bisa membuka file save untuk ditulis"; return false; }
    bool wok = fwrite(out.data(), 1, out.size(), f) == out.size();
    wok = (fflush(f) == 0) && wok;
    wok = (fclose(f) == 0) && wok;
    if (!wok || R_FAILED(fsdevCommitDevice("save"))) {
        err = "penulisan/commit gagal";
        return false;
    }

    // 4. read back and compare
    std::vector<uint8_t> back;
    if (!readAll(path, back) || back != out) {
        // try to restore the original
        FILE* rf = fopen(path.c_str(), "r+b");
        if (rf) { fwrite(orig.data(), 1, orig.size(), rf); fclose(rf); fsdevCommitDevice("save"); }
        err = "verifikasi setelah tulis gagal, save asli dipulihkan dari backup";
        return false;
    }
    orig = out;
    return true;
}

// ---------------------------------------------------------------- save / box view
static void screenSave(const GameDef& g) {
    if (!mountSave(g.titleId)) { notice(NO_BASE, "Gagal mount save", "Save game tidak bisa dibuka. Pastikan game ditutup penuh (tidak suspended).", true); return; }
    std::vector<uint8_t> orig;
    std::string path = std::string("save:/") + g.saveFile, err;
    SaveImage img;
    if (!readAll(path, orig) || !img.load(g, orig, err)) {
        unmountSave();
        notice(NO_BASE, std::string("Tidak bisa memuat save ") + g.name, err + "\nSave TIDAK diubah.", true);
        return;
    }
    const FmtInfo* fi = fmtInfo(g.fmt);
    const int COLS = 6;
    int box = 0, cur = 0;
    bool dirty = false;
    std::vector<bool> taken(g_bank.entries.size(), false);  // bank entries withdrawn, pending removal
    std::vector<uint8_t> mark((size_t)img.boxes() * img.perBox(), 0);  // 1 = added, 2 = removed (pending write)
    std::string msg;

    auto base = [&] {
        ui::begin();
        ui::topBar(std::string(g.name) + "  -  Box " + std::to_string(box + 1) + "/" + std::to_string(img.boxes()),
                   dirty ? "(!) perubahan belum ditulis" : "Bank: " + std::to_string(g_bank.entries.size()) + " Pokémon  ·  " + fi->name,
                   dirty ? ui::WARN : ui::DIM);
        for (int i = 0; i < img.perBox(); i++) {
            PkSummary s = pkSummarize(g.fmt, img.slot(box, i));
            ui::cell(i % COLS, i / COLS, i == cur, s.present, s.name, s.level, mark[(size_t)box * img.perBox() + i]);
        }
        PkSummary c = pkSummarize(g.fmt, img.slot(box, cur));
        if (msg.empty()) ui::text(62, ui::H - 96, "Slot " + std::to_string(cur + 1) + ":  " + (c.present ? describe(g.fmt, img.slot(box, cur)) : std::string("kosong")), 22, ui::TEXT);
        ui::footBar({{"A", "Salin"}, {"X", "Pindah"}, {"Y", "Ambil"}, {"L/R", "Box"}, {"+", "TULIS ke save"}, {"B", "Kembali"}});
        ui::toast(msg);
    };

    while (appletMainLoop()) {
        u64 k = waitKey(base);
        msg.clear();
        if ((k & HidNpadButton_Right) && cur + 1 < img.perBox()) cur++;
        if ((k & HidNpadButton_Left) && cur > 0) cur--;
        if ((k & HidNpadButton_Down) && cur + COLS < img.perBox()) cur += COLS;
        if ((k & HidNpadButton_Up) && cur - COLS >= 0) cur -= COLS;
        if (k & (HidNpadButton_R | HidNpadButton_ZR)) box = (box + 1) % img.boxes();
        if (k & (HidNpadButton_L | HidNpadButton_ZL)) box = (box + img.boxes() - 1) % img.boxes();
        size_t mi = (size_t)box * img.perBox() + cur;

        if (k & (HidNpadButton_A | HidNpadButton_X)) {
            if (!img.present(box, cur)) { msg = "Slot kosong."; continue; }
            BankEntry e;
            e.fmt = g.fmt;
            e.dlc = (uint8_t)(1 + img.dlcLevel());
            e.data.assign(img.slot(box, cur), img.slot(box, cur) + img.slotSize());
            g_bank.entries.push_back(e);
            if (!g_bank.save(BANK_FILE, err)) {
                g_bank.entries.pop_back();
                msg = "GAGAL menyimpan bank: " + err;
                continue;
            }
            taken.push_back(false);
            if (k & HidNpadButton_X) {
                img.clear(box, cur);
                mark[mi] = 2;
                dirty = true;
                msg = "Dipindah ke bank. Tekan + untuk menulis ke save.";
            } else {
                msg = "Disalin ke bank (save tidak berubah).";
            }
        } else if (k & HidNpadButton_Y) {
            if (img.present(box, cur)) { msg = "Slot harus kosong untuk mengambil dari bank."; continue; }
            PickResult pk = pickFromBank(g.fmt, taken, img.dlcLevel());
            int pick = pk.index;
            bool go = pick >= 0;
            if (go && !pk.converted && bankDlcNeeded(g_bank.entries[pick]) > img.dlcLevel()) {
                std::string body = std::string("Pokémon ini disimpan dari save yang memiliki \"") + dlcName(g.fmt, bankDlcNeeded(g_bank.entries[pick])) +
                    "\", sedangkan save ini belum memilikinya.\nJika Pokémon ini memakai konten DLC (spesies, move, ribbon, dsb), memasukkannya bisa membuat game crash atau save bermasalah.";
                if (!confirmA(base, "Peringatan DLC", body, "Tetap masukkan", "Batal", true)) { go = false; msg = "Dibatalkan."; }
            }
            if (go) {
                if (img.put(box, cur, pk.data.data(), pk.data.size())) {
                    taken[pick] = true;
                    mark[mi] = 1;
                    dirty = true;
                    msg = pk.converted ? "Dikonversi dan dimasukkan ke slot. Tekan + untuk menulis ke save." : "Dimasukkan ke slot. Tekan + untuk menulis ke save.";
                } else {
                    msg = "Gagal memasukkan (ukuran tidak cocok).";
                }
            }
        } else if (k & HidNpadButton_Plus) {
            if (!dirty) { msg = "Tidak ada perubahan."; continue; }
            busy(base, "Menulis ke save...", "Jangan matikan konsol.");
            if (commitSave(g, img, orig, err)) {
                // only now drop withdrawn entries from the bank
                std::vector<BankEntry> keep;
                for (size_t i = 0; i < g_bank.entries.size(); i++)
                    if (!taken[i]) keep.push_back(g_bank.entries[i]);
                g_bank.entries = keep;
                taken.assign(g_bank.entries.size(), false);
                std::string e2;
                bool bs = g_bank.save(BANK_FILE, e2);
                // reload image from what is now on disk
                SaveImage fresh;
                if (fresh.load(g, orig, e2)) img = fresh;
                dirty = false;
                std::fill(mark.begin(), mark.end(), 0);
                pruneBackups(g.titleId, KEEP_BACKUPS);
                msg = bs ? "BERHASIL ditulis. Backup tersimpan di sdmc:/switch/Monolit/backups"
                         : "Save ditulis, tapi bank gagal diperbarui (Pokémon mungkin terduplikasi di bank).";
            } else {
                msg = "GAGAL: " + err;
            }
        } else if (k & HidNpadButton_B) {
            if (dirty) {
                if (confirmA(base, "Buang perubahan?", "Ada perubahan yang belum ditulis ke save.", "Buang perubahan", "Tetap di sini", true)) break;
                continue;
            }
            break;
        }
    }
    unmountSave();
}

// ---------------------------------------------------------------- main menu
static const char* TOS_WARNING =
    "Menjalankan CFW dan homebrew melanggar syarat layanan Nintendo. Ada risiko ban online (konsol diblokir dari layanan online). Risiko ditanggung pengguna.\n"
    "Aplikasi ini menulis langsung ke save game. Backup dulu dengan Checkpoint.\n"
    "Monolit adalah proyek penggemar gratis (GPL-3.0), tidak berafiliasi dengan Nintendo, The Pokémon Company, atau GAME FREAK. Tanpa jaminan.";

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    ensureDirs();

    // System shared font (no font file is bundled with the app).
    PlFontData font;
    bool uiOk = R_SUCCEEDED(plInitialize(PlServiceType_User)) && R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard)) &&
                ui::init(font.address, font.size);
    if (!uiOk) {
        consoleInit(NULL);
        printf("Gagal memulai UI grafis (SDL2/font sistem).\nTekan + untuk keluar.\n");
        while (appletMainLoop()) {
            padUpdate(&g_pad);
            if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 1;
    }

    auto quit = [&](int code) {
        ui::shutdown();
        plExit();
        return code;
    };

    if (g_migrationFailed)
        notice(NO_BASE, "Data lama tidak bisa dipindah",
               "Folder lama sdmc:/switch/PokeBank gagal dipindah ke sdmc:/switch/Monolit. Data lama TIDAK dihapus. Pindahkan folder itu secara manual lewat komputer atau file manager, lalu ganti nama pokebank.bank menjadi monolit.bank.", true);

    // Warning shown on every start: ban risk, backup, no affiliation.
    if (!confirmA(NO_BASE, "Peringatan sebelum memakai", TOS_WARNING, "Saya mengerti", "Keluar", true)) return quit(0);

    std::string err;
    if (!g_bank.load(BANK_FILE, err)) {
        notice(NO_BASE, "Bank tidak bisa dimuat",
               err + "\nFile TIDAK diubah dan TIDAK dianggap kosong. Periksa SD card, atau pindahkan/perbaiki dulu:\n" + BANK_FILE + "\n(salinan cadangan: " + BANK_FILE + ".bak)", true);
        return quit(1);
    }

    if (g_bank.recoveredFromBackup()) {
        notice(NO_BASE, "Bank dipulihkan dari salinan cadangan",
               "File bank utama hilang atau rusak. Bank dipulihkan dari " + std::string(BANK_FILE) + ".bak (" + std::to_string(g_bank.entries.size()) +
               " Pokémon).\nPenyimpanan berikutnya akan membuat file utama baru; cadangan tetap aman.");
    }

    // Which Switch user's saves to use: the preselected user (title override) if any; otherwise the only
    // user, or - when several users exist - an explicit choice (the "last opened user" is just the cursor hint).
    Result rc = accountInitialize(AccountServiceType_Application);
    g_uid = AccountUid{};
    if (R_SUCCEEDED(rc)) {
        loadUsers();
        AccountUid pre{};
        if (R_SUCCEEDED(accountGetPreselectedUser(&pre)) && accountUidIsValid(&pre)) {
            g_uid = pre;
        } else if (g_users.size() == 1) {
            g_uid = g_users[0];
        } else if (g_users.size() > 1) {
            AccountUid last{};
            if (R_SUCCEEDED(accountGetLastOpenedUser(&last)) && accountUidIsValid(&last)) g_uid = last;
            screenPickUser(false);
        }
    }

    size_t ng;
    const GameDef* games = gameDefs(ng);
    std::vector<bool> avail(ng, false);
    auto scan = [&]() {
        avail.assign(ng, false);
        busy(NO_BASE, "Memeriksa save game...", "");
        if (!accountUidIsValid(&g_uid)) return;
        for (size_t i = 0; i < ng; i++) {
            if (mountSave(games[i].titleId)) {
                std::string p = std::string("save:/") + games[i].saveFile;
                FILE* f = fopen(p.c_str(), "rb");
                if (f) { avail[i] = true; fclose(f); }
                unmountSave();
            }
        }
    };
    scan();
    std::string uname = accountUidIsValid(&g_uid) ? userName(g_uid) : std::string();

    int cur = 0;
    auto draw = [&] {
        ui::begin();
        ui::topBar(std::string("Monolit v") + APP_VERSION, accountUidIsValid(&g_uid) ? "User: " + uname : "Tidak ada user terdeteksi", accountUidIsValid(&g_uid) ? ui::DIM : ui::WARN);
        for (size_t i = 0; i < ng; i++) {
            const FmtInfo* fi = fmtInfo(games[i].fmt);
            ui::row((int)i, (int)i == cur, games[i].name, avail[i] ? std::string(fi ? fi->name : "") : "save tidak ditemukan", avail[i] ? ui::DIM : ui::WARN);
        }
        ui::text(40, ui::H - 140, "Bank: " + std::to_string(g_bank.entries.size()) + " Pokémon   ·   " + BANK_FILE, 20, ui::DIM);
        ui::text(40, ui::H - 110, "SELALU backup save dengan Checkpoint sebelum menulis.   Proyek penggemar, GPL-3.0, tidak berafiliasi dengan Nintendo / The Pokémon Company.", 17, ui::DIM);
        ui::footBar({{"A", "Buka save"}, {"Y", "Lihat bank"}, {"X", "Ganti user"}, {"+", "Keluar"}});
    };
    while (appletMainLoop()) {
        u64 k = waitKey(draw);
        if (k & HidNpadButton_Plus) break;
        if ((k & HidNpadButton_Down) && cur + 1 < (int)ng) cur++;
        if ((k & HidNpadButton_Up) && cur > 0) cur--;
        if (k & HidNpadButton_Y) screenBank();
        if ((k & HidNpadButton_X) && screenPickUser(true)) {
            scan();
            uname = userName(g_uid);
        }
        if ((k & HidNpadButton_A) && avail[cur]) screenSave(games[cur]);
    }

    accountExit();
    return quit(0);
}
