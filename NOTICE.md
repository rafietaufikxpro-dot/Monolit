# Atribusi dan pemberitahuan pihak ketiga

## PKHeX (GPL-3.0)
Hak cipta Monolit: © 2026 rafietaufik (GPL-3.0). Monolit memakai data dan pengetahuan format yang diturunkan dari **PKHeX** oleh Kurt (kwsch) dan kontributor:
<https://github.com/kwsch/PKHeX> — dilisensikan di bawah GNU GPL v3.0.

| Bagian Monolit | Diturunkan dari (PKHeX) |
|---|---|
| `source/swish_consts.h` (intro, outro, xorpad) | `PKHeX.Core/Saves/Encryption/SwishCrypto/*` |
| `source/games.h` (key blok box, offset BDSP, ukuran slot) | `Saves/Access/SaveBlockAccessor8SWSH/8LA/9SV.cs`, `Saves/SAV8BS.cs`, `Saves/Util/SaveUtil.cs` |
| `source/convert.cpp`, `source/conv_data.*` | `PKM/HOME/GameDataCore.cs`, `GameDataPK8.cs`, `GameDataPB8.cs`, `Game/Locations/LocationsHOME.cs`, `Locations8b.cs`, `Legality/Learnset/Learnset.cs`, `PersonalInfo/*`, `Resources/byte/personal`, `Resources/byte/levelup`, `Moves/MoveInfo8.cs`, `PKM/Util/Experience.cs`, `PKM/Util/Conversion/EntityConverter.cs` |
| `source/pk.cpp` (enkripsi/shuffle Pokémon) | `PKM/Util/PokeCrypto.cs` |
| `source/tables.cpp` (nama spesies, tabel Gen 9) | `PKM/Util/Conversion/SpeciesConverter.cs`, resource nama spesies |

Catat commit PKHeX yang dipakai setiap kali data dibuat ulang.

## Merek dagang
Pokémon dan nama game terkait adalah merek dagang Nintendo, Creatures Inc., dan GAME FREAK inc.
Monolit adalah proyek penggemar independen, **tidak berafiliasi, tidak disponsori, dan tidak didukung**
oleh pihak mana pun tersebut. Proyek ini tidak memuat logo, gambar, sprite, ikon, atau aset resmi.

## Perangkat lunak lain
- SDL2 (zlib) <https://libsdl.org> dan SDL2_ttf (zlib, memakai FreeType: FTL atau GPLv2) — dipakai untuk UI.
- Font: font sistem bawaan Switch dibaca saat runtime lewat service `pl`; tidak disalin atau didistribusikan bersama aplikasi.
- libnx (ISC) — <https://github.com/switchbrew/libnx>
- devkitPro toolchain — <https://devkitpro.org>
- SHA-256 dan MD5 pada `source/hash.cpp` adalah implementasi algoritma standar (FIPS 180-4 / RFC 1321) yang berdiri sendiri. PKHeX memakai kedua hash ini hanya untuk memverifikasi save (lewat pustaka bawaan .NET); aturan pemakaian (apa yang di-hash dan di offset mana) diambil dari PKHeX.
