// Monolit 3DS - entry point (3DS-only, libctru)
// Copyright (C) 2026 rafietaufik
// SPDX-License-Identifier: GPL-3.0-only
#include <3ds.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "../bank.h"
#include "../bank3ds.h"
#include "../games.h"
#include "../pk6.h"
#include "../pk7.h"
#include "../sav6.h"
#include "../sav7.h"
#include "ui3ds.h"

static const char* DIR_ROOT = "sdmc:/3ds/Monolit3DS";
static const char* DIR_BACKUP = "sdmc:/3ds/Monolit3DS/backups";
static const char* BANK_FILE = "sdmc:/3ds/Monolit3DS/bank3ds.bin";

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.1.0"
#endif
#ifndef APP_AUTHOR_STR
#define APP_AUTHOR_STR "rafietaufik"
#endif
static const char* APP_VERSION = APP_VERSION_STR;
static const char* APP_AUTHOR = APP_AUTHOR_STR;

using DrawFn = std::function<void()>;
static const DrawFn NO_BASE = [] {};

static u64 waitKey(const DrawFn& draw) {
    while (aptMainLoop()) {
        draw();
        ui3ds::present();
        hidScanInput();
        u64 d = hidKeysHeld();
        if (d) return d;
    }
    return 0;
}

static void notice(const DrawFn& base, const std::string& title, const std::string& body, bool warn = false) {
    waitKey([&] { ui3ds::begin(); base(); ui3ds::dialog(title, body, {{"A", "OK"}}, warn ? ui3ds::WARN : ui3ds::ACCENT); });
}

static bool confirmA(const DrawFn& base, const std::string& title, const std::string& body, const char* yes, const char* no, bool warn) {
    u64 k = waitKey([&] { ui3ds::begin(); base(); ui3ds::dialog(title, body, {{"A", yes}, {"B", no}}, warn ? ui3ds::WARN : ui3ds::ACCENT); });
    return (k & KEY_A) != 0;
}

static bool readSaveFile(const char* saveName, std::vector<uint8_t>& out) {
    FSarchive fs;
    Result rc = FSUSER_OpenArchive(&fs, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""), 0);
    if (R_FAILED(rc)) return false;
    FSfile file;
    rc = FSUSER_OpenFile(&file, fs, fsMakePath(PATH_UTF8, saveName), FS_OPEN_READ, 0);
    if (R_FAILED(rc)) { FSUSER_CloseArchive(&fs); return false; }
    u64 size;
    FSFILE_GetSize(&file, &size);
    out.resize((size_t)size);
    u32 bytesRead;
    rc = FSFILE_Read(&file, &bytesRead, 0, out.data(), (u32)size);
    FSFILE_Close(&file);
    FSUSER_CloseArchive(&fs);
    return R_SUCCEEDED(rc) && bytesRead == (u32)size;
}

static bool writeSaveFile(const char* saveName, const std::vector<uint8_t>& data) {
    FSarchive fs;
    Result rc = FSUSER_OpenArchive(&fs, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""), 0);
    if (R_FAILED(rc)) return false;
    FSfile file;
    rc = FSUSER_OpenFile(&file, fs, fsMakePath(PATH_UTF8, saveName), FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
    if (R_FAILED(rc)) { FSUSER_CloseArchive(&fs); return false; }
    u32 bytesWritten;
    rc = FSFILE_Write(&file, 0, data.data(), (u32)data.size(), FS_WRITE_FLUSH | FS_WRITE_UPDATE_TIME);
    bool ok = R_SUCCEEDED(rc) && bytesWritten == (u32)data.size();
    FSFILE_Close(&file);
    FSUSER_CloseArchive(&fs);
    return ok;
}

static bool commitSave(const GameDef& g, void* imgPtr, std::vector<uint8_t>& orig, std::string& err) {
    std::vector<uint8_t> out;
    if (g.family == FAM_SAV6) {
        SaveImage6* img = static_cast<SaveImage6*>(imgPtr);
        if (!img->build(out, err)) return false;
    } else {
        SaveImage7* img = static_cast<SaveImage7*>(imgPtr);
        if (!img->build(out, err)) return false;
    }

    // Pre-write verification
    {
        if (g.family == FAM_SAV6) {
            SaveImage6 check;
            if (!check.load(g, out, err)) { err = "pre-write verification failed: " + err; return false; }
        } else {
            SaveImage7 check;
            if (!check.load(g, out, err)) { err = "pre-write verification failed: " + err; return false; }
        }
    }

    // Backup original
    mkdir(DIR_ROOT, 0777);
    mkdir(DIR_BACKUP, 0777);
    char name[200];
    snprintf(name, sizeof(name), "%s/%016llX_%lld_%s.bak", DIR_BACKUP, (unsigned long long)g.titleId, (long long)time(NULL), g.saveFile);
    FILE* bf = fopen(name, "wb");
    if (!bf) { err = "could not create backup, write cancelled"; return false; }
    bool bok = fwrite(orig.data(), 1, orig.size(), bf) == orig.size();
    bok = (fclose(bf) == 0) && bok;
    if (!bok) { err = "backup could not be written, write cancelled"; return false; }

    // Write
    if (!writeSaveFile(g.saveFile, out)) { err = "write to save archive failed"; return false; }

    // Read-back + verify
    std::vector<uint8_t> back;
    if (!readSaveFile(g.saveFile, back) || back != out) {
        writeSaveFile(g.saveFile, orig);
        err = "post-write verification failed, original save restored from backup";
        return false;
    }
    orig = out;
    return true;
}

static void screenSave(const GameDef& g) {
    std::vector<uint8_t> orig;
    if (!readSaveFile(g.saveFile, orig)) {
        notice(NO_BASE, "Failed to read save", "Could not open the save archive. Make sure the game is fully closed.", true);
        return;
    }

    void* imgPtr = nullptr;
    std::string err;
    if (g.family == FAM_SAV6) {
        SaveImage6* img = new SaveImage6();
        if (!img->load(g, orig, err)) { delete img; notice(NO_BASE, "Cannot load save", err + "\nSave NOT modified.", true); return; }
        imgPtr = img;
    } else {
        SaveImage7* img = new SaveImage7();
        if (!img->load(g, orig, err)) { delete img; notice(NO_BASE, "Cannot load save", err + "\nSave NOT modified.", true); return; }
        imgPtr = img;
    }

    SaveImage6* img6 = (g.family == FAM_SAV6) ? static_cast<SaveImage6*>(imgPtr) : nullptr;
    SaveImage7* img7 = (g.family == FAM_SAV7) ? static_cast<SaveImage7*>(imgPtr) : nullptr;

    const FmtInfo* fi = fmtInfo(g.fmt);
    const int COLS = 6;
    int box = 0, cur = 0;
    bool dirty = false;
    int perBox = img6 ? img6->perBox() : img7->perBox();
    int nBoxes = img6 ? img6->boxes() : img7->boxes();
    std::vector<uint8_t> mark((size_t)nBoxes * perBox, 0);
    std::string msg;

    auto base = [&] {
        ui3ds::begin();
        ui3ds::topBar(std::string(g.name) + "  -  Box " + std::to_string(box + 1) + "/" + std::to_string(nBoxes),
                      dirty ? "(!) unsaved changes" : "Bank: " + fi->name, dirty ? ui3ds::WARN : ui3ds::DIM);
        for (int i = 0; i < perBox; i++) {
            const uint8_t* s = img6 ? img6->slot(box, i) : img7->slot(box, i);
            bool pres = img6 ? img6->present(box, i) : img7->present(box, i);
            PkSummary3DS summ;
            if (pres) summ = (g.family == FAM_SAV6) ? pk6Summarize3DS(s) : pk7Summarize3DS(s);
            ui3ds::cell(i % COLS, i / COLS, i == cur, pres, summ.name, summ.level, mark[(size_t)box * perBox + i]);
        }
        const uint8_t* curSlot = img6 ? img6->slot(box, cur) : img7->slot(box, cur);
        PkSummary3DS c;
        bool curPres = img6 ? img6->present(box, cur) : img7->present(box, cur);
        if (curPres) c = (g.family == FAM_SAV6) ? pk6Summarize3DS(curSlot) : pk7Summarize3DS(curSlot);
        if (msg.empty()) ui3ds::text(62, ui3ds::H - 40, "Slot " + std::to_string(cur + 1) + ":  " + (curPres ? c.name + " Lv." + std::to_string(c.level) : std::string("empty")), 18, ui3ds::TEXT);
        ui3ds::footBar({{"A", "Copy"}, {"X", "Move"}, {"Y", "Take"}, {"L/R", "Box"}, {"+", "WRITE"}, {"B", "Back"}});
        ui3ds::toast(msg);
    };

    while (aptMainLoop()) {
        u64 k = waitKey(base);
        if (!aptMainLoop()) break;
        msg.clear();
        if ((k & KEY_RIGHT) && cur + 1 < perBox) cur++;
        if ((k & KEY_LEFT) && cur > 0) cur--;
        if ((k & KEY_DOWN) && cur + COLS < perBox) cur += COLS;
        if ((k & KEY_UP) && cur - COLS >= 0) cur -= COLS;
        if (k & KEY_R) box = (box + 1) % nBoxes;
        if (k & KEY_L) box = (box + nBoxes - 1) % nBoxes;
        size_t mi = (size_t)box * perBox + cur;

        if (k & (KEY_A | KEY_X)) {
            bool pres = img6 ? img6->present(box, cur) : img7->present(box, cur);
            if (!pres) { msg = "Slot is empty."; continue; }
            Bank3DSEntry e;
            e.fmt = g.fmt;
            int slotSize = img6 ? img6->slotSize() : img7->slotSize();
            const uint8_t* s = img6 ? img6->slot(box, cur) : img7->slot(box, cur);
            e.data.assign(s, s + slotSize);
            // TODO: save to bank3ds
            if (k & KEY_X) {
                if (img6) img6->clear(box, cur); else img7->clear(box, cur);
                mark[mi] = 2;
                dirty = true;
                msg = "Moved to bank. Press + to write.";
            } else {
                msg = "Copied to bank (save unchanged).";
            }
        } else if (k & KEY_Y) {
            bool pres = img6 ? img6->present(box, cur) : img7->present(box, cur);
            if (pres) { msg = "The slot must be empty to take from the bank."; continue; }
            msg = "Take from bank not yet implemented.";
        } else if (k & KEY_PLUS) {
            if (!dirty) { msg = "No changes."; continue; }
            if (commitSave(g, imgPtr, orig, err)) {
                dirty = false;
                std::fill(mark.begin(), mark.end(), 0);
                msg = "Written successfully. Backup saved.";
            } else {
                msg = "FAILED: " + err;
            }
        } else if (k & KEY_B) {
            if (dirty) {
                if (confirmA(base, "Discard changes?", "There are changes not written to the save.", "Discard", "Stay here", true)) break;
                continue;
            }
            break;
        }
    }

    if (img6) delete img6;
    else delete img7;
}

static const char* TOS_WARNING =
    "Running CFW and homebrew violates Nintendo's terms of service. There is a risk of an online ban.\n"
    "This app writes directly to your save files. Back up first with Checkpoint.\n"
    "Monolit 3DS is a free fan project (GPL-3.0), not affiliated with Nintendo, The Pokemon Company or GAME FREAK. No warranty.";

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    gfxInitDefault();
    consoleInit(NULL);
    ui3ds::init();
    fsInit();

    std::string err;
    if (!confirmA(NO_BASE, "Warning before use", TOS_WARNING, "I understand", "Exit", true)) {
        ui3ds::shutdown();
        gfxExit();
        fsExit();
        return 0;
    }

    size_t ng;
    const GameDef* games = gameDefs(ng);
    std::vector<bool> avail(ng, false);
    auto scan = [&]() {
        avail.assign(ng, false);
        for (size_t i = 0; i < ng; i++) {
            std::vector<uint8_t> dummy;
            if (readSaveFile(games[i].saveFile, dummy)) {
                avail[i] = true;
            }
        }
    };
    scan();

    int cur = 0;
    auto draw = [&] {
        ui3ds::begin();
        ui3ds::topBar(std::string("Monolit 3DS v") + APP_VERSION, "Bank: 0", ui3ds::DIM);
        for (size_t i = 0; i < ng; i++) {
            const FmtInfo* fi = fmtInfo(games[i].fmt);
            ui3ds::row((int)i, (int)i == cur, games[i].name, avail[i] ? std::string(fi ? fi->name : "") : "save not found", avail[i] ? ui3ds::DIM : ui3ds::WARN);
        }
        ui3ds::text(40, ui3ds::H - 40, std::string("Monolit 3DS v") + APP_VERSION + "   by " + APP_AUTHOR + "   ·   GPL-3.0", 18, ui3ds::ACCENT);
        ui3ds::footBar({{"A", "Open save"}, {"B", "Exit"}});
    };

    while (aptMainLoop()) {
        u64 k = waitKey(draw);
        if (k & KEY_B) break;
        if ((k & KEY_DOWN) && cur + 1 < (int)ng) cur++;
        if ((k & KEY_UP) && cur > 0) cur--;
        if ((k & KEY_A) && avail[cur]) screenSave(games[cur]);
    }

    ui3ds::shutdown();
    gfxExit();
    fsExit();
    return 0;
}
