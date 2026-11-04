# Contributing to Monolit

Thanks for your interest in helping! Monolit is a `.nro` homebrew for Switch CFW (Atmosphère) that works as an offline
Pokémon bank. Because it **writes directly to real game save files**, its safety standard is stricter than usual.
Please read this document before opening a PR.

## Contents
- [Core principles](#core-principles)
- [How to help](#how-to-help)
- [Setting up](#setting-up)
- [Build and test](#build-and-test)
- [Code layout](#code-layout)
- [Contribution flow](#contribution-flow)
- [Pull request checklist](#pull-request-checklist)
- [Adding a game or format](#adding-a-game-or-format)
- [Reporting bugs](#reporting-bugs)
- [Code and commit style](#code-and-commit-style)
- [AI-assisted contributions](#ai-assisted-contributions)
- [License](#license)
- [Disclaimer and trademark rules](#disclaimer-and-trademark-rules)

## Core principles

1. **Never damage a user's save.** Do not weaken the existing protections: changes stay in memory until the user presses **+**,
   hashes/checksums are verified, a backup is always created before writing, the written result is read back and compared, and
   new bank entries are removed only after the save was written successfully.
2. **Do not guess data.** Every offset, block key, size and table must be traceable to [PKHeX](https://github.com/kwsch/PKHeX).
   Name the source file in the PR description.
3. **Be honest about test status.** Never write "tested" or "compatible" for something that has not run on a real console.
4. **Keep the core portable.** Files other than `source/main.cpp` and `source/ui.cpp` must not include libnx/SDL, so the PC tests keep working.

## How to help

- **Test on a real console** (most needed): run the test checklist in `AI_CONTEXT.md` §8 and report the results.
- Fix open issues (see `AI_CONTEXT.md` §7), for example more accurate DLC checks (legality), party/daycare support,
  duplicate markers, and a configurable backup count. The v0.2 features (user picker, DLC warning, bank recovery from `.bak`,
  backup pruning, cross-game conversion) still need testing on a real console.
- Add automated tests, especially with real save fixtures that you obtained yourself and share safely.
- Translations, documentation, app icon.

## Setting up

You need:
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the packages `switch-dev` (devkitA64 + libnx), `switch-sdl2`, `switch-sdl2_ttf`, `switch-pkg-config` to build the `.nro`
- `g++` (C++17) and `make` for the PC tests
- A Switch with Atmosphère for real testing, plus [Checkpoint](https://github.com/FlagBrew/Checkpoint) to back up saves

```bash
git clone https://github.com/<username>/Monolit.git
cd Monolit
export DEVKITPRO=/opt/devkitpro
```

## Build and test

```bash
# Core tests on PC (must be green before a PR)
make -f Makefile.host test

# Build the .nro for Switch
make
# output: Monolit.nro  -> copy to sdmc:/switch/
```

Build output (`build/`, `*.elf`, `*.nro`, `*.nacp`) is an artifact and is in `.gitignore` (so are `*.bank` and `*.bak`, so
personal banks/backups are never committed). After changing the version/author run `make clean && make`.

Without devkitPro you can still syntax-check `main.cpp` against the libnx headers (`-fsyntax-only -D__SWITCH__ -I<libnx>/nx/include`),
but that is **not** a replacement for a full build.

## Code layout

| File | Role |
|---|---|
| `source/main.cpp` | Screen flow + save mount/commit (Switch only, libnx) |
| `source/ui.cpp/.h` | UI drawing (SDL2 + SDL2_ttf, Switch only; no libnx, no image/font assets) |
| `source/sav.cpp/.h` | Load, verify, modify and rebuild save files (portable) |
| `source/pk.cpp/.h` | Read-only Pokémon summary and Gen 8 slot crypto (portable) |
| `source/convert.cpp/.h` | Cross-game conversion, PK8 <-> PB8 (portable) |
| `source/conv_data.*` | Conversion tables generated from PKHeX by `tools/gen_convert_tables.py`. **Do not edit by hand.** |
| `source/bank.cpp/.h` | `PKBANK01` bank file format (portable) |
| `source/hash.cpp/.h` | SHA-256 and MD5 (portable) |
| `source/games.h` | Game table: title ID, save file name, block keys, offsets |
| `source/swish_consts.h`, `source/tables.cpp` | Data generated from PKHeX. **Do not edit by hand.** |
| `test/test_core.cpp`, `test/test_convert.cpp` | PC unit tests |
| `test/golden_dump.cpp`, `tools/golden_compare.py`, `tools/golden/` | Golden test against PKHeX (see `tools/GOLDEN.md`) |

The full binary-format description and data flow are in [`AI_CONTEXT.md`](AI_CONTEXT.md).

## Contribution flow

1. Fork the repo and branch from `main`: `git checkout -b fix/bank-recover-bak`
2. Make a **small, focused** change (one problem per PR).
3. Run `make -f Makefile.host test` before and after the change.
4. If you touch `commitSave`, `SaveImage::build` or `Bank::save`, you **must** add/adjust tests.
5. Commit with a clear message, push, and open a Pull Request.
6. Respond to review. PRs that touch the save-write path get a closer review.

## Pull request checklist

Copy into the PR description:

```markdown
## Summary
<what changed and why>

## Data source (if you add/change an offset, key, size or table)
<PKHeX file + line / other source>

## Testing
- [ ] `make -f Makefile.host test` passes
- [ ] `.nro` build succeeds (`make`)
- [ ] Tested on a real console (state: game, game version, Atmosphère/firmware version)
- [ ] Tested on PC only (not on a console)

## Safety
- [ ] Does not weaken: `+` confirmation, hash verification, backup, read-back, bank entry removal order
- [ ] Does not add libnx dependencies to portable files
- [ ] No saves, ROMs or keys were committed
```

## Adding a game or format

1. Find the reference implementation in PKHeX (save block accessor, slot size, box block key, Pokémon crypto).
2. Add the `Fmt` and `FmtInfo` (slot size, nickname/level offsets) and a `GameDef` entry in `source/games.h`
   (official title ID, save file name, family, keys/offsets, number of boxes and slots).
3. If the format is new (not SCBlock/BDSP), add a new family in `sav.cpp` with strict hash/checksum verification.
4. Add round-trip tests in `test/test_core.cpp` (load → modify → build → reload → compare), and tests that a corrupted save is rejected.
5. Update the README (games table) and `AI_CONTEXT.md`.
6. Do not enable a new game in a release before it has been tested with a real save.

For a new **conversion pair**, follow `AI_CONTEXT.md` §5 rule 8 and `tools/GOLDEN.md`: generated tables, a golden comparison
against PKHeX, host tests, and the UI warning.

## Reporting bugs

Open an Issue with:

- The game, game version (update number), and number of users on the console
- Atmosphère and firmware versions
- Monolit version (see the menu title) and how you launch it (applet/album or title override)
- The steps you took and the error message shown on screen
- Whether the save can still be loaded by the game afterwards

**Data safety:** do not attach save files, ROMs, `prod.keys` or firmware dumps to public Issues. If the problem concerns a save
that cannot be loaded, state the file size and error message, and we will find a safe way to share it.

For vulnerabilities or bugs that can corrupt a save without warning: report them first through an Issue labelled `safety`
(or contact the maintainer) and avoid including detailed steps that could hurt other users before a fix exists.

## Code and commit style

- C++17, no RTTI, no exceptions (as set in the `Makefile`).
- Follow the style of the surrounding file: 4-space indentation, English comments, English UI text.
- Avoid unnecessary large allocations; RAM for applet-mode homebrew is limited.
- Check every I/O return value (`fopen`, `fwrite`, `fclose`, `Result`) and return a clear error message to the UI.
- Short, descriptive commit messages, for example:
  - `fix(bank): recover from .bak when the main file is missing`
  - `feat(ui): add user profile picker`
  - `test(sav): add truncated save case`

## AI-assisted contributions

AI assistants are welcome, provided that:
- You understand and have tested the code you submit, and you state that AI was used if most of the PR was generated by it.
- The AI must not "invent" offsets, keys or sizes. Everything must be verified against PKHeX.
- Give `AI_CONTEXT.md` to your assistant as project context.

## License

Monolit is released under **GPL-3.0** because offsets, block keys, encryption and tables come from PKHeX (GPL-3.0).
By submitting a contribution you agree that it is released under the same license.
Do not include code or data whose license is incompatible with GPL-3.0, or copyrighted material (ROMs, console keys, game assets).
The full license text is in [`LICENSE`](LICENSE).

## Disclaimer and trademark rules

This project is **not affiliated with, sponsored by, or endorsed by** Nintendo, The Pokémon Company, Creatures Inc. or GAME FREAK inc.
Pokémon and related game names are trademarks of their respective owners.

- Do not add official logos, images, sprites, icons, music or other assets (including in the UI, the `.nro` icon and the documentation).
  The UI uses its own plain text and generic shapes.
- Do not sell Monolit or charge money using the Pokémon name or brand.
- By using CFW/homebrew, users violate Nintendo's ToS and take on the risk of an online ban. Do not write documentation that
  promises "ban-safe" use.
- `.nro` releases must follow [`RELEASING.md`](RELEASING.md) (source + `LICENSE` + `NOTICE.md`). Attribution is in [`NOTICE.md`](NOTICE.md).
