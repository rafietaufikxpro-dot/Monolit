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
#include "sidedata.h"
#include "ui.h"

static const char* DIR_ROOT = "sdmc:/switch/Monolit";
static const char* DIR_BACKUP = "sdmc:/switch/Monolit/backups";
static const char* BANK_FILE = "sdmc:/switch/Monolit/monolit.bank";
static const char* SIDES_FILE = "sdmc:/switch/Monolit/sides.dat";

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.2.0"
#endif
#ifndef APP_AUTHOR_STR
#define APP_AUTHOR_STR "rafietaufik"
#endif
static const char* APP_VERSION = APP_VERSION_STR;
static const char* APP_AUTHOR = APP_AUTHOR_STR;
static const int KEEP_BACKUPS = 10;  // newest backups kept per game

static PadState g_pad;
static AccountUid g_uid;
static Bank g_bank;
static SideStore g_sides;  // original per-game data of converted Pokemon (see sidedata.h)

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
        ui::topBar("Select user", "Saves are read per Switch user");
        if (cur < top) top = cur;
        if (cur >= top + ui::LIST_ROWS) top = cur - ui::LIST_ROWS + 1;
        for (int i = top; i < n && i < top + ui::LIST_ROWS; i++) ui::row(i - top, i == cur, std::to_string(i + 1) + "   " + names[i], "");
        std::vector<ui::Hint> h{{"A", "Use this user"}};
        if (allowCancel) h.push_back({"B", "Cancel"});
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
        if (n == 0) ui::text(ui::W / 2, 300, "Bank is empty", 28, ui::DIM, 1);
        ui::footBar({{"Up/Down", "Select"}, {"Left/Right", "±10"}, {"X", "Delete from bank"}, {"B", "Back"}});
        ui::toast(msg);
    };
    while (appletMainLoop()) {
        u64 k;
        if (confirmDel) {
            k = waitKey([&] { base(); ui::dialog("Delete permanently?", "This entry will be deleted PERMANENTLY from the bank and cannot be recovered.", {{"X", "Delete"}, {"B", "Cancel"}}, ui::BAD); });
        } else {
            k = waitKey(base);
        }
        int n = (int)g_bank.entries.size();
        if (confirmDel) {
            confirmDel = false;
            if ((k & HidNpadButton_X) && n > 0) {
                g_bank.entries.erase(g_bank.entries.begin() + cur);
                std::string err;
                msg = g_bank.save(BANK_FILE, err) ? "Entry deleted." : ("FAILED to save bank: " + err);
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
    std::vector<uint8_t> srcSlot;   // the bank entry's original slot (kept as side data once the save is written)
};

// Lists bank entries of this format plus convertible ones (PK8 <-> PB8). Converted data is computed once up front.
static PickResult pickFromBank(Fmt fmt, const std::vector<bool>& taken, int haveDlc) {
    struct Row { int bank; conv::Result res; std::vector<uint8_t> data; bool restored = false; };
    std::vector<Row> rows;
    for (size_t i = 0; i < g_bank.entries.size(); i++) {
        const BankEntry& e = g_bank.entries[i];
        if (taken[i]) continue;
        if (e.fmt == fmt) { rows.push_back(Row{(int)i, conv::OK, {}, false}); continue; }
        if (!conv::supported(e.fmt, fmt)) continue;
        Row r{(int)i, conv::OK, {}, false};
        SideKey key;
        const SideEntry* se = sideKeyOf(e.fmt, e.data.data(), key) ? g_sides.find(fmt, key) : nullptr;  // earlier visit to this game?
        r.res = conv::convertSlot(e.fmt, fmt, e.data.data(), r.data, se ? se->slot.data() : nullptr, &r.restored);
        rows.push_back(std::move(r));
    }
    int cur = 0, top = 0;
    int n = (int)rows.size();
    auto draw = [&] {
        ui::begin();
        ui::topBar("Take from bank", std::to_string(n) + " usable in this game");
        if (cur < top) top = cur;
        if (cur >= top + ui::LIST_ROWS) top = cur - ui::LIST_ROWS + 1;
        for (int i = top; i < n && i < top + ui::LIST_ROWS; i++) {
            const BankEntry& be = g_bank.entries[rows[i].bank];
            std::string right;
            ui::Col col = ui::WARN;
            if (be.fmt != fmt) {
                const FmtInfo* sf = fmtInfo(be.fmt);
                if (rows[i].res == conv::OK) right = std::string("CONVERT from ") + (sf ? sf->name : "?") + (rows[i].restored ? " + original data" : "");
                else {
                    switch (rows[i].res) {
                        case conv::FOREIGN_ORIGIN: right = "CANNOT: original data missing"; break;
                        case conv::NOT_IN_GAME: right = "CANNOT: not in this game"; break;
                        case conv::EGG: right = "CANNOT: egg"; break;
                        case conv::NOT_TRANSFERABLE: right = "CANNOT: not allowed"; break;
                        default: right = "CANNOT: see A"; break;
                    }
                    col = ui::BAD;
                }
            } else if (bankDlcNeeded(be) > haveDlc) {
                right = std::string("NEEDS ") + dlcName(fmt, bankDlcNeeded(be));
            }
            ui::row(i - top, i == cur, std::to_string(i + 1) + "   " + describe(be.fmt, be.data.data()), right, col);
        }
        if (n == 0) ui::text(ui::W / 2, 300, "No Pokémon in the bank fit this game", 26, ui::DIM, 1);
        else ui::text(40, ui::H - 90, "CONVERT = offline HOME simulation: moves are reset to the destination game's learnset; for offline use only.", 18, ui::DIM);
        ui::footBar({{"Up/Down", "Select"}, {"A", "Take"}, {"B", "Cancel"}});
    };
    while (appletMainLoop()) {
        u64 k = waitKey(draw);
        if (k & HidNpadButton_B) return {};
        if ((k & HidNpadButton_A) && n) {
            const BankEntry& be = g_bank.entries[rows[cur].bank];
            PickResult pr;
            pr.index = rows[cur].bank;
            pr.from = be.fmt;
            pr.srcSlot = be.data;
            if (be.fmt == fmt) { pr.data = be.data; return pr; }
            if (rows[cur].res != conv::OK) { notice(draw, "Cannot convert", conv::describe(rows[cur].res), true); continue; }
            const FmtInfo* sf = fmtInfo(be.fmt);
            const FmtInfo* df = fmtInfo(fmt);
            std::string body = std::string("From ") + (sf ? sf->name : "?") + " to " + (df ? df->name : "?") + ".\n" +
                (rows[cur].restored
                    ? "This Pokémon has been in the destination game before: its original moves, ball, location and ability are restored from the data Monolit kept. The held item is removed.\n"
                    : "Moves are reset to what the destination game gives at this level, ability/ball/location are adjusted and the held item is removed. Monolit keeps a copy of the original data so it can be restored if the Pokémon returns to its original game.\n") +
                "The result has no valid HOME tracker: use it OFFLINE ONLY (not with real HOME, trades or online battles).";
            if (fmt == FMT_PK8) body += "\nIf this Pokémon only exists in the Sword/Shield DLC, make sure the destination save has that DLC.";
            if (!confirmA(draw, "Cross-game conversion", body, "Convert", "Cancel", true)) continue;
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
        if (!check.load(g, out, err)) { err = "pre-write verification failed: " + err; return false; }
    }

    // 2. backup original save to SD (never skipped)
    char name[160];
    snprintf(name, sizeof(name), "%s/%s_%lld_%s.bak", DIR_BACKUP, hex64(g.titleId).c_str(), (long long)time(NULL), g.saveFile);
    FILE* bf = fopen(name, "wb");
    if (!bf) { err = "could not create the backup, write cancelled"; return false; }
    bool bok = fwrite(orig.data(), 1, orig.size(), bf) == orig.size();
    bok = (fclose(bf) == 0) && bok;
    if (!bok) { err = "backup could not be written, write cancelled"; return false; }

    // 3. write + commit
    std::string path = std::string("save:/") + g.saveFile;
    FILE* f = fopen(path.c_str(), "r+b");
    if (!f) { err = "could not open the save file for writing"; return false; }
    bool wok = fwrite(out.data(), 1, out.size(), f) == out.size();
    wok = (fflush(f) == 0) && wok;
    wok = (fclose(f) == 0) && wok;
    if (!wok || R_FAILED(fsdevCommitDevice("save"))) {
        err = "write/commit failed";
        return false;
    }

    // 4. read back and compare
    std::vector<uint8_t> back;
    if (!readAll(path, back) || back != out) {
        // try to restore the original
        FILE* rf = fopen(path.c_str(), "r+b");
        if (rf) { fwrite(orig.data(), 1, orig.size(), rf); fclose(rf); fsdevCommitDevice("save"); }
        err = "post-write verification failed, original save restored from backup";
        return false;
    }
    orig = out;
    return true;
}

// ---------------------------------------------------------------- save / box view
static void screenSave(const GameDef& g) {
    if (!mountSave(g.titleId)) { notice(NO_BASE, "Failed to mount save", "The save could not be opened. Make sure the game is fully closed (not suspended).", true); return; }
    std::vector<uint8_t> orig;
    std::string path = std::string("save:/") + g.saveFile, err;
    SaveImage img;
    if (!readAll(path, orig) || !img.load(g, orig, err)) {
        unmountSave();
        notice(NO_BASE, std::string("Cannot load save of ") + g.name, err + "\nThe save was NOT modified.", true);
        return;
    }
    const FmtInfo* fi = fmtInfo(g.fmt);
    const int COLS = 6;
    int box = 0, cur = 0;
    bool dirty = false;
    std::vector<bool> taken(g_bank.entries.size(), false);  // bank entries withdrawn, pending removal
    struct PendingSide { Fmt fmt; std::vector<uint8_t> slot; };
    std::vector<PendingSide> pendingSides;                   // original slots of converted Pokemon, stored once the save is written
    std::vector<uint8_t> mark((size_t)img.boxes() * img.perBox(), 0);  // 1 = added, 2 = removed (pending write)
    std::string msg;

    auto base = [&] {
        ui::begin();
        ui::topBar(std::string(g.name) + "  -  Box " + std::to_string(box + 1) + "/" + std::to_string(img.boxes()),
                   dirty ? "(!) unsaved changes" : "Bank: " + std::to_string(g_bank.entries.size()) + " Pokémon  ·  " + fi->name,
                   dirty ? ui::WARN : ui::DIM);
        for (int i = 0; i < img.perBox(); i++) {
            PkSummary s = pkSummarize(g.fmt, img.slot(box, i));
            ui::cell(i % COLS, i / COLS, i == cur, s.present, s.name, s.level, mark[(size_t)box * img.perBox() + i]);
        }
        PkSummary c = pkSummarize(g.fmt, img.slot(box, cur));
        if (msg.empty()) ui::text(62, ui::H - 96, "Slot " + std::to_string(cur + 1) + ":  " + (c.present ? describe(g.fmt, img.slot(box, cur)) : std::string("empty")), 22, ui::TEXT);
        ui::footBar({{"A", "Copy"}, {"X", "Move"}, {"Y", "Take"}, {"L/R", "Box"}, {"+", "WRITE to save"}, {"B", "Back"}});
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
            if (!img.present(box, cur)) { msg = "Slot is empty."; continue; }
            BankEntry e;
            e.fmt = g.fmt;
            e.dlc = (uint8_t)(1 + img.dlcLevel());
            e.data.assign(img.slot(box, cur), img.slot(box, cur) + img.slotSize());
            g_bank.entries.push_back(e);
            if (!g_bank.save(BANK_FILE, err)) {
                g_bank.entries.pop_back();
                msg = "FAILED to save bank: " + err;
                continue;
            }
            taken.push_back(false);
            if (k & HidNpadButton_X) {
                img.clear(box, cur);
                mark[mi] = 2;
                dirty = true;
                msg = "Moved to bank. Press + to write to the save.";
            } else {
                msg = "Copied to bank (save unchanged).";
            }
        } else if (k & HidNpadButton_Y) {
            if (img.present(box, cur)) { msg = "The slot must be empty to take from the bank."; continue; }
            PickResult pk = pickFromBank(g.fmt, taken, img.dlcLevel());
            int pick = pk.index;
            bool go = pick >= 0;
            if (go && !pk.converted && bankDlcNeeded(g_bank.entries[pick]) > img.dlcLevel()) {
                std::string body = std::string("This Pokémon was stored from a save that has \"") + dlcName(g.fmt, bankDlcNeeded(g_bank.entries[pick])) +
                    "\", but this save does not.\nIf this Pokémon uses DLC content (species, moves, ribbons, etc.), adding it can crash the game or corrupt the save.";
                if (!confirmA(base, "DLC warning", body, "Add anyway", "Cancel", true)) { go = false; msg = "Cancelled."; }
            }
            if (go) {
                if (img.put(box, cur, pk.data.data(), pk.data.size())) {
                    taken[pick] = true;
                    mark[mi] = 1;
                    if (pk.converted) pendingSides.push_back({pk.from, pk.srcSlot});
                    dirty = true;
                    msg = pk.converted ? "Converted and placed in the slot. Press + to write to the save." : "Placed in the slot. Press + to write to the save.";
                } else {
                    msg = "Could not place it (size mismatch).";
                }
            }
        } else if (k & HidNpadButton_Plus) {
            if (!dirty) { msg = "No changes."; continue; }
            busy(base, "Writing to save...", "Do not turn off the console.");
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
                bool sidesOk = true;
                if (!pendingSides.empty()) {  // keep each converted Pokemon's original data so it can go back to its first game intact
                    for (const PendingSide& ps : pendingSides) g_sides.put(ps.fmt, ps.slot.data());
                    pendingSides.clear();
                    std::string e3;
                    sidesOk = g_sides.save(SIDES_FILE, e3);
                }
                msg = !bs ? "Save written, but the bank could not be updated (the Pokémon may be duplicated in the bank)."
                    : !sidesOk ? "Written, but the original data of converted Pokémon could not be saved (they can't be restored later)."
                               : "Written successfully. Backup saved in sdmc:/switch/Monolit/backups";
            } else {
                msg = "FAILED: " + err;
            }
        } else if (k & HidNpadButton_B) {
            if (dirty) {
                if (confirmA(base, "Discard changes?", "There are changes that have not been written to the save.", "Discard", "Stay here", true)) break;
                continue;
            }
            break;
        }
    }
    unmountSave();
}

// ---------------------------------------------------------------- main menu
static const char* TOS_WARNING =
    "Running CFW and homebrew violates Nintendo's terms of service. There is a risk of an online ban (console blocked from online services). You use this at your own risk.\n"
    "This app writes directly to your save files. Back up first with Checkpoint.\n"
    "Monolit is a free fan project (GPL-3.0), not affiliated with Nintendo, The Pokémon Company or GAME FREAK. No warranty.";

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
        printf("Failed to start the graphical UI (SDL2/system font).\nPress + to exit.\n");
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
        notice(NO_BASE, "Old data could not be moved",
               "The old folder sdmc:/switch/PokeBank could not be moved to sdmc:/switch/Monolit. The old data was NOT deleted. Move the folder manually with a computer or file manager, then rename pokebank.bank to monolit.bank.", true);

    // Warning shown on every start: ban risk, backup, no affiliation.
    if (!confirmA(NO_BASE, "Warning before use", TOS_WARNING, "I understand", "Exit", true)) return quit(0);

    std::string err;
    if (!g_bank.load(BANK_FILE, err)) {
        notice(NO_BASE, "The bank could not be loaded",
               err + "\nThe file was NOT modified and was NOT treated as empty. Check the SD card, or move/repair it first:\n" + BANK_FILE + "\n(backup copy: " + BANK_FILE + ".bak)", true);
        return quit(1);
    }

    if (g_bank.recoveredFromBackup()) {
        notice(NO_BASE, "Bank restored from backup copy",
               "The main bank file is missing or damaged. The bank was restored from " + std::string(BANK_FILE) + ".bak (" + std::to_string(g_bank.entries.size()) +
               " Pokémon).\nThe next save will create a new main file; the backup copy stays intact.");
    }

    {   // Original per-game data of converted Pokemon. A damaged file must never block the app or be silently overwritten.
        std::string e4;
        if (!g_sides.load(SIDES_FILE, e4)) {
            const std::string bak = std::string(SIDES_FILE) + ".bak";
            if (g_sides.load(bak, e4)) {
                notice(NO_BASE, "Side data restored from backup", "sides.dat was damaged; the previous copy (sides.dat.bak) is being used.");
            } else {
                rename(SIDES_FILE, (std::string(SIDES_FILE) + ".corrupt").c_str());
                g_sides.entries.clear();
                notice(NO_BASE, "Side data file is damaged",
                       e4 + "\nIt was renamed to sides.dat.corrupt and Monolit continues without it. Pokémon converted earlier can no longer get their original data back.", true);
            }
        }
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
        busy(NO_BASE, "Checking save games...", "");
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
        ui::topBar(std::string("Monolit v") + APP_VERSION, accountUidIsValid(&g_uid) ? "User: " + uname : "No user detected", accountUidIsValid(&g_uid) ? ui::DIM : ui::WARN);
        for (size_t i = 0; i < ng; i++) {
            const FmtInfo* fi = fmtInfo(games[i].fmt);
            ui::row((int)i, (int)i == cur, games[i].name, avail[i] ? std::string(fi ? fi->name : "") : "save not found", avail[i] ? ui::DIM : ui::WARN);
        }
        ui::text(40, ui::H - 170, std::string("Monolit v") + APP_VERSION + "   by " + APP_AUTHOR + "   ·   GPL-3.0", 22, ui::ACCENT);
        ui::text(40, ui::H - 140, "Bank: " + std::to_string(g_bank.entries.size()) + " Pokémon   ·   " + BANK_FILE, 20, ui::DIM);
        ui::text(40, ui::H - 110, "ALWAYS back up your saves with Checkpoint before writing.   Fan project, GPL-3.0, not affiliated with Nintendo / The Pokémon Company.", 17, ui::DIM);
        ui::footBar({{"A", "Open save"}, {"Y", "View bank"}, {"X", "Switch user"}, {"+", "Exit"}});
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
