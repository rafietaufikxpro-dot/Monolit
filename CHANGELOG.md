# Changelog

## 0.2.0 — 2026-11-02
Hardening release. **Not yet tested on real hardware.**

### Fixed
- **Bank data loss:** `Bank::load` treated any failure to open the file as an empty bank, and `Bank::save` deleted the `.bak`
  first, so two saves in a row after a transient SD read error could destroy the previous bank. Now only a truly missing file
  (no `.bak`) is empty; I/O errors and damaged files are errors; the new file is written and read back before anything is replaced.
- **Missing/damaged main bank file** (e.g. power loss between the two renames) is recovered from `monolit.bank.bak`,
  and the good `.bak` is left untouched until the next normal save.
- **Wrong Switch user:** no more silent fallback to the first profile. A single user is automatic; with several users a picker
  is shown (also via **X** in the menu).

### Added
- DLC warning (Sword/Shield: Isle of Armor, Crown Tundra; Scarlet/Violet: Teal Mask, Indigo Disk) using the same block-presence
  rules as PKHeX. Bank entries now carry a DLC tag in the formerly reserved header byte (v0.1 files remain valid).
- Automatic backup retention (newest 10 per game).
- Tests for bank recovery, I/O-error handling, DLC tag round trip and DLC detection; sanitizer-clean.
- Optional `icon.jpg` support, `APP_AUTHOR` override, `.gitignore`.

### Changed
- Default author is "Monolit contributors" (was "you"). Version bumped to 0.2.0.

## 0.1.0
- Initial version: copy/move/withdraw between saves and an offline bank for SwSh, BDSP, LA, SV.

## Unreleased
- **Nintendo 3DS port (Gen 6/7 offline bank):** new `source/3ds/` (libctru, dual-screen UI with 6×5 box grid), `bank3ds.*` (`MONO3D01` bank at `sdmc:/3ds/Monolit3DS/bank3ds.bin`), `pk6`/`pk7` and `sav6`/`sav7` (box crypto + checksum), and `Makefile.3ds` for devkitARM. Save writes are verified read-back with backups to `sdmc:/3ds/Monolit3DS/backups`, and the app shows a ToS/ban-risk warning on startup. **Not yet tested on real hardware.** Reading/writing the bank file from the save screen is still TODO.
- Data safety: `SideStore` now recovers a missing or damaged `sides.dat` from `sides.dat.bak`, and keeps the good backup when saving again (same rule as the bank). Previously a missing main file was treated as an empty store and the backup was ignored, so converted Pokémon could lose their original per-game data.
- Safety: `waitKey` no longer returns the PLUS button when the applet closes. That sentinel could make the save screen write to the save while the applet was exiting; it now returns "no button", and the save screen also checks that the applet is still running before acting on input.
- Cross-game conversion phase 3: Sword/Shield <-> Scarlet/Violet (PK8 <-> PK9). Tera Type imported from the species type, scale/size handled, round trips via `sides.dat`. The picker shows when a species needs The Teal Mask / The Indigo Disk (from the SV dex data) and the confirmation warns if the destination save lacks it. Golden-tested against PKHeX (8000 random cases, both directions, byte-identical with no ranges ignored).
- Cross-game conversion phase 2: Sword/Shield <-> Legends: Arceus (PK8 <-> PA8). Alpha mark -> Alpha, grit values start at 0, move mastery not guessed. Round trips through `sides.dat` work for PA8 too. Slot crypto now handles PA8 (0x58-byte blocks). Golden-tested against PKHeX (2000 random cases per direction) apart from legality-derived relearn/mastery data.
- Round trips SwSh <-> BD/SP: original per-game data of converted Pokémon is kept in `sides.dat` and restored when the Pokémon returns to that game (HOME-like). A Pokémon with a HOME placeholder location and no stored original data is still refused. New `sidedata.cpp/.h`, tests in `test/test_convert.cpp`.
- Author/version now come from one place (`APP_AUTHOR`/`APP_VERSION` in the Makefile): shown in the homebrew menu via the `.nacp` and in the app menu ("Monolit vX by <author>"). The `.nacp` is now rebuilt whenever the Makefile changes (before, a stale `.nacp` could keep showing "Unspecified Author").
- Renamed the project from PokeBank to Monolit (trademark caution). Data folder is now `sdmc:/switch/Monolit` and the bank file `monolit.bank`; an existing `sdmc:/switch/PokeBank` folder is moved automatically on first start (nothing is deleted).
- Cross-game conversion phase 1: PK8 <-> PB8 (`convert.cpp`, generated `conv_data.*`, `tools/gen_convert_tables.py`), shown in the withdraw list as KONVERSI with a confirmation dialog. Verified against PKHeX with a golden test (`tools/GOLDEN.md`).
- GPL-3.0 (SPDX) header in all source files; `tools/release.sh` builds a release package that includes the source; copyright/author: rafietaufik.
- All user-facing text (app UI, error messages, docs) is now English.
- UI: replaced the libnx text console with an SDL2/SDL2_ttf graphical UI (`source/ui.cpp`): 6x5 box grid, pending-change markers, dialogs, startup warning (ban risk/backup/no affiliation). System font, no bundled assets. Save/commit/bank logic unchanged. **Needs hardware testing** (only syntax-checked against libnx headers; rendering previewed on PC).
- Makefile: links SDL2/SDL2_ttf via devkitPro pkg-config.
- Add GPL-3.0 `LICENSE`, `NOTICE.md` (PKHeX attribution), `RELEASING.md`; README: ToS/ban risk and trademark notes; CONTRIBUTING: no-affiliation disclaimer and asset rules.
