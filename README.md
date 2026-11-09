# Monolit

**An offline Pokémon bank for Nintendo Switch CFW.** Copy or move Pokémon
between supported game saves and a bank on your SD card—no internet connection
or Pokémon HOME required.

> **Development warning:** Monolit is still in development and has not been
> tested on a real console with real saves. Features and behavior may change,
> and data loss is possible. Use only with backed-up saves and at your own risk.
> See [Safety and limitations](#safety-and-limitations) before using it.
>
> **Warning:** Monolit modifies game saves and runs on consoles with custom
> firmware. Always make a backup and understand the risks before using it.
> This project is not affiliated with Nintendo, Creatures Inc., or GAME FREAK.

## Features

- Store Pokémon in a local bank on your SD card.
- Copy or move Pokémon between the bank and supported game saves.
- Select a Switch user when more than one user is available.
- Write saves with backup and verification.
- Early-stage conversion between Sword/Shield and Brilliant Diamond/Shining Pearl.

## Supported games

| Game | Pokémon format | Save filename |
|---|---|---|
| Pokémon Sword / Shield | PK8 | `main` |
| Pokémon Brilliant Diamond / Shining Pearl | PB8 | `SaveData.bin` |
| Pokémon Legends: Arceus | PA8 | `main` |
| Pokémon Scarlet / Violet | PK9 | `main` |

Not yet supported: Let's Go Pikachu/Eevee and Pokémon Legends: Z-A.

Pokémon in the bank can be returned to games that use the same format (for example,
Sword ↔ Shield or Scarlet ↔ Violet). The only cross-game conversion available is
Sword/Shield ↔ Brilliant Diamond/Shining Pearl; see [Cross-game conversion](#cross-game-conversion).


## Controls

### Menu

| Button | Action |
|---|---|
| Up / Down | Select a save |
| A | Open save |
| Y | View bank |
| X | Change user |
| + | Exit |

### Pokémon box

| Button | Action |
|---|---|
| D-pad | Move between slots |
| A | Copy Pokémon to the bank |
| X | Move Pokémon to the bank |
| Y | Retrieve a Pokémon from the bank into an empty slot |
| L / R | Change box |
| + | Write changes to save |
| B | Go back |

Each box displays 6 × 5 slots. Changed slots are outlined in yellow for added
Pokémon and red for removed Pokémon. The UI uses SDL2 and the Switch system
font; no bundled assets are required.

## Cross-game conversion

Supported pairs: Pokémon Sword/Shield ↔ Brilliant Diamond/Shining Pearl, and
Pokémon Sword/Shield ↔ Legends: Arceus, and Pokémon Sword/Shield ↔ Scarlet/Violet,
as long as the species and form are available in the destination game. Following Pokémon HOME rules via PKHeX,
moves are adjusted to the destination game's learnset and held items are removed.
Moving directly between Brilliant Diamond/Shining Pearl, Legends: Arceus and
Scarlet/Violet is not supported (go through Sword/Shield).

Legends: Arceus specifics: Pokémon carrying the Alpha mark become Alphas, grit
values start at zero, and move-mastery flags are not guessed (they start empty).

Scarlet/Violet specifics: the Tera Type is set from the species' type (as Pokémon HOME
does), size follows the Pokémon's scale, and the list shows when a species needs
The Teal Mask or The Indigo Disk and your save does not have it (you get a warning
before converting).

Eggs and Nincada/Spinda (to/from Brilliant Diamond/Shining Pearl) are rejected.
Converted Pokémon are for offline use only and do not have a valid HOME tracker.

**Going back and forth:** like Pokémon HOME, Monolit keeps each Pokémon's original
per-game data (moves, ball, met location, ability, game version) in
`sdmc:/switch/Monolit/sides.dat` once a converted Pokémon has been written to a save.
If the Pokémon later returns to that game, its original data is restored instead of
being rebuilt (for example a Brilliant Diamond Pokémon moved to Sword and back is
a Brilliant Diamond Pokémon again). A Pokémon that was converted earlier but whose
original data is not on this SD card is rejected rather than guessed. Do not delete
`sides.dat` if you move Pokémon between these games.

## Switch users and DLC

Saves are read for the selected Switch user. If there is only one user, it is
selected automatically. If there are multiple users (for example, when the
application is launched from the Album/applet), select a user at startup or
press **X** in the menu to change users.

When placing a Pokémon into a save, Monolit compares the DLC in the source save
(when the Pokémon was stored in the bank) with the destination save. The DLC
checked is:

- Sword/Shield: Isle of Armor and Crown Tundra
- Scarlet/Violet: The Teal Mask and The Indigo Disk

If the destination save does not appear to have the required DLC, Monolit asks
for confirmation. This is a heuristic warning only, not a legality check.

## Safety and limitations

- Changes are kept in memory until you press **+**.
- Before writing, the hash/checksum is verified and a save backup is created in
  `sdmc:/switch/Monolit/backups/`. The written save is read back and compared.
- New bank entries are removed only after the save is successfully written.
- The 10 most recent backups per game are kept; older backups are cleaned up
  automatically.
- Bank files are written atomically and read back for verification. If the
  primary file is missing or damaged, the bank is restored from
  `monolit.bank.bak`. A bank file that cannot be read (for example, due to an SD
  card error) is never treated as empty.
- Back up your save with Checkpoint before first use.
- Pokémon added to a save are not registered in the Pokédex. Avoid moving
  Pokémon currently in your battle team.
- A warning about ban risks, backups, and affiliation is shown every time the
  application starts.

Monolit has not been tested on a real console with real saves. Automated tests
cover only core logic on PC. See [`AI_CONTEXT.md`](AI_CONTEXT.md) for the test
checklist before using Monolit with important saves.

## Important warnings

- **Online service risk:** Running CFW and homebrew violates Nintendo's
  ToS/EULA and may result in a console being banned from online services,
  including Nintendo Switch Online. Use at your own risk.
- **No warranty:** This application writes to game saves. Always make a backup.
- **Not an official product:** Pokémon and related game names are trademarks of
  their respective owners. Monolit is not affiliated with them and is not for
  sale.

## License and attribution

Monolit is released under the **GNU GPL v3.0**. Offsets, block keys,
encryption, and tables are derived from [PKHeX](https://github.com/kwsch/PKHeX),
which is licensed under GPL-3.0. See [`LICENSE`](LICENSE) and
[`NOTICE.md`](NOTICE.md) for license details and attributions. Every `.nro`
release must include its source; see [`RELEASING.md`](RELEASING.md).

```text
Monolit - offline Pokemon bank for Switch CFW
Copyright (C) 2026 rafietaufik

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License v3.0.
This program comes with ABSOLUTELY NO WARRANTY.
```

Monolit was previously named PokeBank. The old data folder
`sdmc:/switch/PokeBank` is automatically moved to `sdmc:/switch/Monolit` the
first time the application starts. The name “Monolit” was chosen to be neutral;
if a trademark holder objects, the project name can be changed.
