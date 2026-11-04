# AI_CONTEXT.md — Monolit

> Briefing for AI coding agents (Claude, Codex, Copilot, Cursor, etc.) working on this repo.
> Rename to `AGENTS.md` / `CLAUDE.md` if your tool expects that name.
> Last reviewed: 2026-11-02 (against PKHeX `master`, libnx 4.12.0, Atmosphère 1.12.1). Code state: **v0.2.0**.

## 1. What this project is

Monolit is a **homebrew `.nro` app for Nintendo Switch custom firmware (Atmosphère)**.
It is an **offline, Pokémon HOME–like bank**: it copies/moves Pokémon between a game's
save file and a bank file on the SD card, with no internet and no Nintendo services.

- Version: v0.2.0 (early; **not yet validated on real hardware with real saves**). See `CHANGELOG.md`.
- UI: SDL2 + SDL2_ttf (1280x720 logical), gamepad only via libnx `pad`; font = Switch system shared font (`plGetSharedFontByType`), no bundled assets. UI strings are English.
- Code comments / identifiers: English.
- License: **GPL-3.0** (offsets, block keys, crypto and tables are derived from PKHeX, GPL-3.0).  Files: `LICENSE`, `NOTICE.md`, `RELEASING.md` (releases must ship source).

### Supported games (see `source/games.h`)

| Game | Format | Save file | Family | Boxes × slots | Slot bytes |
|---|---|---|---|---|---|
| Sword / Shield | PK8 | `main` | SCBlock | 32 × 30 | 0x158 |
| Brilliant Diamond / Shining Pearl | PB8 | `SaveData.bin` | BDSP | 40 × 30 | 0x158 |
| Legends: Arceus | PA8 | `main` | SCBlock | 32 × 30 | 0x168 |
| Scarlet / Violet | PK9 | `main` | SCBlock | 32 × 30 | 0x158 |

Not supported: Let's Go Pikachu/Eevee, Legends: Z-A, party/daycare/other storage. Cross-format conversion: ONLY PK8 <-> PB8 (SwSh <-> BDSP), see §4.6; PA8/PK9 are not convertible yet.
A bank entry can only be returned to a save of the **same format** (Sword↔Shield, Scarlet↔Violet, BD↔SP).

## 2. Repo map

```
Makefile            devkitPro build (devkitA64 + libnx). Needs $DEVKITPRO.
Makefile.host       host (PC) unit tests: `make -f Makefile.host test`
source/
  main.cpp          Switch-only screen flow + save mount/commit (libnx). NOT portable.
  ui.cpp/.h         Switch-only drawing helpers (SDL2/SDL2_ttf, no libnx). Not part of host tests.
  sav.cpp/.h        SaveImage: load/verify/modify/rebuild save files. Portable.
  pk.cpp/.h         Read-only Pokémon summary (decrypt a COPY for display). Portable.
  bank.cpp/.h       Bank file format (PKBANK01) load/save. Portable.
  hash.cpp/.h       SHA-256 + MD5, self-contained. Portable.
  games.h           GameDef table (title IDs, save names, block keys, offsets) + FmtInfo.
  swish_consts.h    SwishCrypto intro/outro/xorpad (generated from PKHeX).
  tables.cpp/.h     Species names + Gen 9 internal→national table (GENERATED, do not hand-edit).
test/test_core.cpp  Host tests for the portable core.
```

Everything except `main.cpp` and `ui.cpp` must stay **platform independent** (no libnx includes) so it keeps building on a PC.
Build outputs (`build/`, `*.elf`, `*.nro`, `*.nacp`) are artifacts — do not commit them.

## 3. Data flow

```
Start → mount each game's save (fsOpen_SaveData + fsdevMountDevice "save")  [main.cpp]
      → read whole save file into memory
      → SaveImage::load(): verify hash/checksum, locate box area, decrypt box area into box_
      → user edits in memory only (copy / move / withdraw)
      → [+] pressed → commitSave():
           1. SaveImage::build() → full file with new box area + recomputed hash
           2. SaveImage::load() on the result (self-verify)
           3. write backup of the ORIGINAL to sdmc:/switch/Monolit/backups/
           4. fopen("r+b") + fwrite + fflush + fclose + fsdevCommitDevice("save")
           5. read back and compare; on mismatch restore original
      → only after success: remove withdrawn entries from the bank and save bank (atomic tmp+rename+.bak)
```

## 4. Binary formats

### 4.1 Bank file `sdmc:/switch/Monolit/monolit.bank`

```
"PKBANK01" | u32 count | u32 reserved
per entry: u8 fmt | u8 dlc | u16 size | size bytes   (little endian)
```
`dlc` (formerly a reserved zero byte, so v0.1 files stay valid): `0` = unknown, otherwise `1 + DLC level` of the save the Pokémon was
deposited from. Unknown values are preserved on rewrite. `.bak` is the previous good copy (see §5).
Entry bytes are the **raw, still-encrypted slot** exactly as stored in the save (so no re-encryption is needed).
`fmt` values: 1=PK8, 2=PB8, 3=PA8, 4=PK9. `size` must equal `FmtInfo.slotSize`.

### 4.2 SCBlock family (SwSh, LA, SV) — "SwishCrypto"

1. Last 32 bytes = SHA-256 over `INTRO(64) || payload || OUTRO(64)`, payload = file minus last 32 bytes.
2. Payload has a static XOR layer: `byte[i] ^= XORPAD[i % 0x7F]` (pad array is 0x80 long, stride is 0x7F).
3. Decrypted payload is a sequence of blocks: `u32 key | u8 type^xs | …` where `xs` is `SCXorShift32(key)`.
   Types: 1–3 no data; 4 object (`i32 len ^ xs`, then data); 5 array (`i32 count ^ xs`, `u8 subtype ^ xs`, data);
   3/8/12 → 1 byte; 9/13 → 2; 10/14/16 → 4; 11/15/17 → 8.
4. Block **data bytes are XOR'd with the continuing xorshift stream**. The box block key is
   `0x0d66012c` (SwSh, SV) and `0x47E1CEAB` (LA). Box area = first `boxes*perBox*slotSize` bytes of that block.
5. The walker must end **exactly** at the payload end; otherwise the save is rejected.

### 4.3 BDSP family (BD/SP)

- Valid file sizes: `0xE9828, 0xEDC20, 0xEED8C, 0xEF0A4`.
- MD5 of the whole file, computed with the 16-byte hash field at `0xE9818` zeroed (same offset for every revision).
- Box area at fixed offset `0x14EF4`, plaintext (boxes are stored as encrypted PB8 slots).

### 4.4 DLC level detection (`SaveImage::dlcLevel`, SCBlock saves only)

Same rules as PKHeX (presence of DLC blocks with non-empty data):

| Game | Level 1 | Level 2 |
|---|---|---|
| Sword/Shield | block `0x3F936BA9` (Isle of Armor zukan) present, `0x3C9366F0` absent | both present (Crown Tundra) |
| Scarlet/Violet | block `0x100B93DA` (Kitakami raid data) present | block `0x66A33824` (Blueberry points) present — checked first |

BDSP and Legends: Arceus have no DLC (always 0). The UI only **warns** when a Pokémon from a higher-DLC save is put into a lower-DLC save
(heuristic, not a legality check).

### 4.5 Pokémon slot (PK8/PB8/PA8/PK9)

- `ec = u32 @0`, `sv = (ec >> 13) & 31`. Words from `0x08` to `storedEnd` are XOR'd with an LCRNG stream
  (`seed = 0x41C64E6D*seed + 0x6073; x = seed >> 16`) and blocks are shuffled by `BLOCK_POS[(sv%24)*4 + i]`.
  `storedEnd` = `0x148` (block 0x50) or `0x168` (PA8, block 0x58). Party-stat tail (if any) uses the same stream restarted.
- Slot is "present" iff `u32 @0 != 0 || u16 @8 != 0`.
- Species `u16 @0x08` (after decrypt). Gen 9 uses internal IDs: if `species-917` is in table range, `species += TABLE9[shift]`.
- Display only: nickname UTF-16LE (13 chars) at `0x58` (`0x60` for PA8); level byte at `0x148` (not present for PA8 box slots).
- **The app never modifies Pokémon contents.** It only moves opaque slot bytes.

### 4.6 Conversion (PK8 <-> PB8) — `source/convert.cpp`, `source/conv_data.*` (generated)
HOME model: shared "core" fields are copied (EC, PID, IVs/EVs, nature, ribbons, OT/HT, tracker as-is...), per-game data is rebuilt:
ability from the target personal table by ability number, moves reset to the target learnset at the current level (PP from the
Gen 8 PP table, PP Ups 0), relearn cleared, ball kept (PK8 target: > Beast -> Poke Ball), locations mapped (PB8 local <-> mainline,
BD/SP origin -> SwSh magic location 59999/59998 with version remapped to SW/SH), held item dropped, party stats recomputed.
Refused: eggs, species/form not "present in game" for the target, Nincada/Spinda to/from PB8, PK8 with HOME magic met location.
Verified byte-for-byte against PKHeX for 6000 synthetic cases (`tools/GOLDEN.md`); only known difference: PKHeX's legality-based relearn guess.
Not covered: real-device behaviour, legality, HOME service acceptance.

## 5. Safety invariants — do not break these

1. Nothing is written to the save until the user presses **+**.
2. Before any write: hash/checksum verified on load, result self-verified, **backup always written first**
   (abort if the backup cannot be written).
3. After writing: read back and compare; restore the original on mismatch.
4. Bank entries are removed **only after** the save write succeeded. Bank is saved atomically (tmp + read-back + rename + `.bak`).
   `Bank::load` must **never** turn an unreadable/invalid file into an empty bank: only a truly missing file (no `.bak` either) is "empty";
   a missing/damaged main file with a valid `.bak` is recovered, and `save()` then leaves that good `.bak` untouched.
5. Only the box area is changed; everything else in the file is carried through byte-for-byte.
6. Never "fix up" or guess offsets: every offset, key and size must trace back to a PKHeX source file (see §9).
7. Keep the core free of libnx so the host tests keep running.
8. Cross-format conversion is a HOME *simulation* modelled on PKHeX (GPL-3.0). Add a new pair only with: generated tables (`tools/gen_convert_tables.py`), a golden comparison against PKHeX (`tools/GOLDEN.md`), host tests, and the UI warning (offline only, no valid HOME tracker; never fabricate a tracker). Eggs, species not present in the target game, and already-transferred (HOME magic location) Pokémon are refused. A bad Pokémon can crash a game or corrupt a save.

## 6. Compatibility report (Atmosphère)

Verdict: **technically compatible, not yet proven on hardware.** Static review and build artifacts look correct;
no real console/save test has been done.

### Verified (static/automated)

| Item | Result |
|---|---|
| Toolchain | Built with devkitA64 + **libnx 4.12.0** (newlib 4.6.0, 2026). Standard flags `-march=armv8-a+crc+crypto -mtp=soft -fPIE`. |
| Output | `Monolit.nro` has valid `NRO0` header and `ASET` section; `Monolit.nacp` is 0x4000 bytes (title `Monolit`, author `you`, v`0.1.0`). No icon embedded. |
| libnx APIs | `fsOpen_SaveData(FsFileSystem*, u64, AccountUid)`, `accountGetPreselectedUser`, `accountGetLastOpenedUser`, `accountListAllUsers` exist with matching signatures in libnx master. |
| SwishCrypto constants | INTRO / OUTRO / XORPAD **byte-identical** to PKHeX `SwishCrypto.cs`; pad stride 0x7F matches. |
| Block keys | `0x0d66012c` (SwSh, SV) and `0x47E1CEAB` (LA) match PKHeX `SaveBlockAccessor*`. |
| SCBlock type codes | Match PKHeX `SCTypeCode` (1–3, 4, 5, 8–17). |
| Slot sizes | 0x158 (SwSh/BDSP/SV) and 0x168 (LA box slot) match PKHeX `SIZE_BOXSLOT`; box counts 32/40/32/32 match. |
| BDSP | Sizes, hash offset `0xE9818` and box offset `0x14EF4` match PKHeX `SAV8BS` / `SaveUtil`. |
| Pokémon crypto | LCRNG, shuffle table (`BlockPosition`), block sizes 0x50/0x58 match PKHeX `PokeCrypto`. |
| Gen 9 species table | `TABLE9_INTERNAL_TO_NATIONAL` (109 entries) identical to PKHeX `SpeciesConverter.Table9InternalToNational`. |
| Title IDs | All 7 title IDs are the public application IDs. |
| Host tests | `make -f Makefile.host test` → `ALL TESTS PASSED` (SHA-256/MD5 known vectors, round-trip of every game family, tamper detection, bank file incl. `.bak` recovery / I/O-error / DLC-tag cases, DLC detection). Also clean under ASan + UBSan. |
| Switch code | `main.cpp` passes a `-fsyntax-only -Wall -Wextra` check against the real libnx v4.12.0 headers (cross g++ with a stub `sys/lock.h`). This is **not** a full devkitPro build/link: always run `make` with devkitPro before release. |

### Caveat on the tests
`test/test_core.cpp` uses an encoder written by the same author mirroring the same understanding of the format.
It proves internal consistency, **not** compatibility with real game saves. Add real (privately obtained) save
fixtures or cross-check against PKHeX output before claiming full support.

### Environment facts (checked 2026-11-02)
- Latest Atmosphère seen: **1.12.1** (bundles hbl 2.4.5 / hbmenu 3.6.1); community tools report support for firmware up to 23.x.
- Homebrew NROs built with a recent libnx generally keep working across firmware updates; a rebuild against the newest libnx is the safe default after a major firmware bump.
- Requires a CFW-launched homebrew environment (hbmenu/hbl). It will not run on stock firmware.
- The target game must be **fully closed** (not running/suspended) or the save cannot be opened.
- Launch via hbmenu in applet mode (e.g. album hijack) or title override; both are handled by the account fallback chain below.

## 7. Known issues / TODO (prioritized)

**Fixed in v0.2.0** (see `CHANGELOG.md`): bank data-loss path on read errors, no user picker, no DLC warning, unbounded backups,
placeholder author, missing `.bak` recovery. These are covered by host tests where the logic is portable; the UI parts
(`main.cpp`: user picker, DLC prompt, backup pruning) are only syntax-checked and **need hardware testing**.

**Still open**
1. **No hardware/real-save validation.** Test each game on a real console with a Checkpoint backup (checklist in §8).
2. **DLC warning is a heuristic.** It compares the DLC level of the source and target saves, not what the Pokémon actually uses.
   It can warn needlessly or miss a case (e.g. old bank entries with `dlc == 0` never warn). A real check would need legality data (species/move/ribbon/met-location tables from PKHeX).
3. Pokédex is not updated on insert; no legality check; Pokémon in battle teams (esp. BDSP) should not be moved (BDSP team slots reference box positions).
4. Only boxes are handled — no party, daycare, or other storage.
5. "Copy to bank" (A) creates duplicates by design; consider a visual marker or a warning.
6. User names are printed with the libnx console font; non-ASCII nicknames may render as garbage (the list index still works).
7. UI paging uses a fixed 34 rows; verify on handheld and docked console sizes.
8. No app icon by default (drop a 256×256 `icon.jpg` next to the Makefile). `APP_AUTHOR` defaults to "rafietaufik".
9. Backups: the newest `KEEP_BACKUPS` (10) per game are kept; older ones are deleted automatically after a successful write. Make configurable if users ask.
10. Missing games: Let's Go (different format), Legends: Z-A (needs new keys/format — only add with a verified PKHeX source).

## 8. Hardware test checklist

1. Back up all saves with Checkpoint and keep a copy on a PC.
2. User picker: with 2+ profiles, the picker appears (when launched via album/applet); choose a user, confirm saves belong to that user; test **X** (change user) in the menu. With 1 profile it must be automatic.
3. Per game: open save → box view shows plausible species/levels/nicknames matching the in-game boxes.
4. Copy (A) one Pokémon → bank file grows, save unchanged. Verify with PKHeX.
5. Move (X) → press **+** → boot the game, confirm the slot is empty and the game loads without errors.
6. Withdraw (Y) into an empty slot → **+** → boot game → confirm the Pokémon is present and usable.
7. Open the written save in PKHeX: no checksum/hash error, Pokémon legal.
8. Power-loss / failure paths: remove SD mid-way only on a throwaway save.
9. Test Sword↔Shield and Scarlet↔Violet round trips between two saves.
10. DLC prompt: deposit from a save with DLC, withdraw into a save without it → warning appears, `[A]` proceeds, any other button cancels.
11. Bank recovery: copy `monolit.bank` away, delete the main file, relaunch → "Bank restored from backup copy" notice and entries intact.
12. Backups: after 11+ successful writes to one game, only the 10 newest `.bak` files for that title ID remain.

## 9. Commands & references

```bash
# Host unit tests (PC)
make -f Makefile.host test

# Switch build
export DEVKITPRO=/opt/devkitpro
make            # → Monolit.nro (+ .elf, .nacp); use `make clean && make` after changing metadata
make clean
make APP_AUTHOR="Your Name"   # override the author shown in the homebrew menu
```

References (source of truth for every constant):
- PKHeX `PKHeX.Core/Saves/Encryption/SwishCrypto/*` (hash, xorpad, SCBlock types, xorshift)
- PKHeX `PKHeX.Core/Saves/Access/SaveBlockAccessor8SWSH.cs`, `…8LA.cs`, `…9SV.cs` (block keys)
- PKHeX `PKHeX.Core/Saves/SAV8BS.cs`, `Saves/Util/SaveUtil.cs` (BDSP offsets, sizes, MD5)
- PKHeX `PKHeX.Core/PKM/Util/PokeCrypto.cs`, `PKM/Util/Conversion/SpeciesConverter.cs`
- libnx: `nx/include/switch/services/fs.h`, `acc.h`

## 10. Rules for AI agents

- Prefer small, reviewable diffs. Run `make -f Makefile.host test` before and after changes.
- Do not hand-edit `tables.cpp` or `swish_consts.h`; regenerate from PKHeX and note the PKHeX commit.
- Never invent offsets, keys or sizes. Cite the PKHeX file/line you took them from in the PR description.
- Do not weaken a safety invariant (§5) to make a feature easier. If a change touches `commitSave`, `SaveImage::build`, or `Bank::save`, add a host test.
- Do not commit save files, ROMs, `prod.keys`, or other copyrighted/private data.
- Keep UI text in English (current convention) unless the maintainer adds localization.
- Be honest in docs: do not claim "tested" or "compatible" for anything not run on real hardware.
