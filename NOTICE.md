# Attribution and third-party notices

## PKHeX (GPL-3.0)
Monolit copyright: © 2026 rafietaufik (GPL-3.0). Monolit uses data and format knowledge derived from **PKHeX** by Kurt (kwsch)
and contributors: <https://github.com/kwsch/PKHeX>, licensed under the GNU GPL v3.0.

| Monolit part | Derived from (PKHeX) |
|---|---|
| `source/swish_consts.h` (intro, outro, xorpad) | `PKHeX.Core/Saves/Encryption/SwishCrypto/*` |
| `source/games.h` (box block keys, BDSP offsets, slot sizes) | `Saves/Access/SaveBlockAccessor8SWSH/8LA/9SV.cs`, `Saves/SAV8BS.cs`, `Saves/Util/SaveUtil.cs` |
| `source/convert.cpp`, `source/conv_data.*` | `PKM/HOME/GameDataCore.cs`, `GameDataPK8.cs`, `GameDataPB8.cs`, `GameDataPA8.cs`, `PKM/PA8.cs`, `PersonalInfo8LA`, `Resources/byte/personal/personal_la`, `Resources/byte/levelup/lvlmove_la.pkl`, `Moves/MoveInfo8a.cs`, `Game/Locations/LocationsHOME.cs`, `Locations8b.cs`, `Legality/Learnset/Learnset.cs`, `PersonalInfo/*`, `Resources/byte/personal`, `Resources/byte/levelup`, `Moves/MoveInfo8.cs`, `PKM/Util/Experience.cs`, `PKM/Util/Conversion/EntityConverter.cs` |
| `source/pk.cpp` (Pokémon encryption/shuffle) | `PKM/Util/PokeCrypto.cs` |
| `source/tables.cpp` (species names, Gen 9 tables) | `PKM/Util/Conversion/SpeciesConverter.cs`, species name resources |

Record the PKHeX commit used every time the generated data is rebuilt.

## Trademarks
Pokémon and related game names are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc.
Monolit is an independent fan project and is **not affiliated with, sponsored by, or endorsed by** any of them.
This project contains no official logos, images, sprites, icons or assets.

## Other software
- SDL2 (zlib) <https://libsdl.org> and SDL2_ttf (zlib, uses FreeType: FTL or GPLv2), used for the UI.
- Font: the Switch built-in system font is read at runtime through the `pl` service; it is not copied or distributed with the app.
- libnx (ISC): <https://github.com/switchbrew/libnx>
- devkitPro toolchain: <https://devkitpro.org>
- SHA-256 and MD5 in `source/hash.cpp` are standalone implementations of the standard algorithms (FIPS 180-4 / RFC 1321).
  PKHeX only uses these hashes to verify saves (through the .NET standard library); the rules for what is hashed and at which
  offset come from PKHeX.
