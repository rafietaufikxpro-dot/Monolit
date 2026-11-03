# Berkontribusi ke Monolit

Terima kasih sudah tertarik membantu! Monolit adalah homebrew `.nro` untuk Switch CFW (Atmosphère)
yang berfungsi seperti bank Pokémon offline. Karena aplikasi ini **menulis langsung ke file save game**,
standar keamanannya lebih ketat daripada proyek biasa. Mohon baca dokumen ini sebelum membuka PR.

> **English summary:** Monolit writes to real save files, so safety comes first. Fork → branch → make a small change →
> run `make -f Makefile.host test` → open a PR with a clear description. Every offset/key must cite a PKHeX source.
> Never attach saves, ROMs, or keys. Bug reports and PRs may be written in Indonesian or English.

## Daftar isi
- [Prinsip utama](#prinsip-utama)
- [Cara membantu](#cara-membantu)
- [Menyiapkan lingkungan](#menyiapkan-lingkungan)
- [Build dan tes](#build-dan-tes)
- [Struktur kode](#struktur-kode)
- [Alur kontribusi](#alur-kontribusi)
- [Checklist Pull Request](#checklist-pull-request)
- [Menambah game atau format baru](#menambah-game-atau-format-baru)
- [Melaporkan bug](#melaporkan-bug)
- [Gaya kode dan commit](#gaya-kode-dan-commit)
- [Kontribusi dengan bantuan AI](#kontribusi-dengan-bantuan-ai)
- [Lisensi](#lisensi)

## Prinsip utama

1. **Save pengguna tidak boleh rusak.** Jangan melemahkan perlindungan yang sudah ada:
   perubahan hanya di memori sampai pengguna menekan **+**, hash/checksum diverifikasi, backup selalu dibuat
   sebelum menulis, hasil tulisan dibaca ulang dan dibandingkan, dan entri bank baru dihapus hanya setelah save berhasil ditulis.
2. **Jangan menebak data.** Setiap offset, key blok, ukuran, dan tabel harus bisa ditelusuri ke sumber
   [PKHeX](https://github.com/kwsch/PKHeX). Sebutkan file sumbernya di deskripsi PR.
3. **Jujur soal status pengujian.** Jangan menulis "sudah dites" atau "kompatibel" untuk sesuatu yang belum dijalankan di konsol asli.
4. **Inti tetap portabel.** Semua file selain `source/main.cpp` dan `source/ui.cpp` tidak boleh meng-include libnx/SDL agar tes di PC tetap berjalan.

## Cara membantu

- **Tes di konsol asli** (paling dibutuhkan): jalankan checklist pengujian di `AI_CONTEXT.md` §8 dan laporkan hasilnya.
- Memperbaiki masalah yang masih terbuka (lihat `AI_CONTEXT.md` §7), misalnya: pengecekan DLC yang lebih akurat (legalitas),
  dukungan party/daycare, penanda duplikat, dan pengaturan jumlah backup. Fitur v0.2 (pemilih user, peringatan DLC,
  pemulihan bank dari `.bak`, pembersihan backup) masih perlu diuji di konsol asli.
- Menambah tes otomatis, terutama dengan fixture save asli yang kamu peroleh sendiri dan dibagikan secara aman.
- Terjemahan, dokumentasi, ikon aplikasi.

## Menyiapkan lingkungan

Yang dibutuhkan:
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) dengan paket `switch-dev` (devkitA64 + libnx), `switch-sdl2`, `switch-sdl2_ttf`, `switch-pkg-config` untuk build `.nro`
- `g++` (C++17) dan `make` untuk tes di PC
- Konsol Switch dengan Atmosphère untuk pengujian nyata, plus [Checkpoint](https://github.com/FlagBrew/Checkpoint) untuk backup save

```bash
git clone https://github.com/<username>/Monolit.git
cd Monolit
export DEVKITPRO=/opt/devkitpro
```

## Build dan tes

```bash
# Tes inti di PC (wajib hijau sebelum PR)
make -f Makefile.host test

# Build .nro untuk Switch
make
# hasil: Monolit.nro  -> salin ke sdmc:/switch/
```

Hasil build (`build/`, `*.elf`, `*.nro`, `*.nacp`) adalah artefak dan sudah ada di `.gitignore` (begitu juga `*.bank` dan
`*.bak`, supaya bank/backup pribadi tidak ikut ter-commit). Setelah mengubah versi/author jalankan `make clean && make`.

Tanpa devkitPro, kamu masih bisa memeriksa sintaks `main.cpp` terhadap header libnx dengan cross-compiler `aarch64-linux-gnu-g++`
(`-fsyntax-only -D__SWITCH__ -I<libnx>/nx/include`), tetapi itu **bukan** pengganti build penuh.

## Struktur kode

| File | Peran |
|---|---|
| `source/main.cpp` | Alur layar + mount/commit save (khusus Switch, libnx) |
| `source/ui.cpp/.h` | Menggambar UI (SDL2 + SDL2_ttf, khusus Switch; tanpa libnx, tanpa aset gambar/font) |
| `source/sav.cpp/.h` | Muat, verifikasi, ubah, dan bangun ulang file save (portabel) |
| `source/pk.cpp/.h` | Ringkasan Pokémon hanya-baca (portabel) |
| `source/bank.cpp/.h` | Format file bank `PKBANK01` (portabel) |
| `source/hash.cpp/.h` | SHA-256 dan MD5 (portabel) |
| `source/games.h` | Tabel game: title ID, nama file save, key blok, offset |
| `source/swish_consts.h`, `source/tables.cpp` | Data hasil generate dari PKHeX. **Jangan diedit manual.** |
| `test/test_core.cpp` | Tes unit di PC |

Penjelasan format biner dan alur data lengkap ada di [`AI_CONTEXT.md`](AI_CONTEXT.md).

## Alur kontribusi

1. Fork repo dan buat branch dari `main`: `git checkout -b fix/bank-recover-bak`
2. Buat perubahan yang **kecil dan fokus** (satu masalah per PR).
3. Jalankan `make -f Makefile.host test` sebelum dan sesudah perubahan.
4. Jika menyentuh `commitSave`, `SaveImage::build`, atau `Bank::save`, **wajib** menambah/menyesuaikan tes.
5. Commit dengan pesan jelas, push, lalu buka Pull Request.
6. Tanggapi review. PR yang menyentuh jalur tulis save akan di-review lebih teliti.

## Checklist Pull Request

Salin ke deskripsi PR:

```markdown
## Ringkasan
<apa yang berubah dan kenapa>

## Sumber data (jika menambah/mengubah offset, key, ukuran, tabel)
<file + baris PKHeX / sumber lain>

## Pengujian
- [ ] `make -f Makefile.host test` lulus
- [ ] Build `.nro` berhasil (`make`)
- [ ] Diuji di konsol asli (sebutkan: game, versi game, versi Atmosphère/firmware)
- [ ] Hanya diuji di PC (tidak diuji di konsol)

## Keamanan
- [ ] Tidak melemahkan: konfirmasi `+`, verifikasi hash, backup, baca-ulang, urutan hapus entri bank
- [ ] Tidak menambah dependensi libnx di file portabel
- [ ] Tidak ada save, ROM, atau key yang ikut ter-commit
```

## Menambah game atau format baru

1. Cari implementasi acuannya di PKHeX (accessor blok save, ukuran slot, key blok box, crypto Pokémon).
2. Tambahkan `Fmt` dan `FmtInfo` (ukuran slot, offset nickname/level) serta entri `GameDef` di `source/games.h`
   (title ID resmi, nama file save, family, key/offset, jumlah box dan slot).
3. Jika formatnya baru (bukan SCBlock/BDSP), tambahkan family baru di `sav.cpp` dengan verifikasi hash/checksum yang ketat.
4. Tambahkan tes round-trip di `test/test_core.cpp` (muat → ubah → bangun → muat ulang → bandingkan) dan tes save yang dirusak harus ditolak.
5. Perbarui README (tabel game) dan `AI_CONTEXT.md`.
6. Jangan mengaktifkan dukungan game baru di rilis sebelum diuji pada save asli.

## Melaporkan bug

Buka Issue dengan informasi berikut:

- Game, versi game (angka update), dan jumlah user di konsol
- Versi Atmosphère dan firmware
- Versi Monolit (lihat judul menu) dan cara memasang/menjalankannya (applet/album atau title override)
- Langkah yang dilakukan dan pesan error yang tampil di layar
- Apakah save masih bisa dimuat game setelahnya

**Keamanan data:** jangan melampirkan file save, ROM, `prod.keys`, atau dump firmware di Issue publik.
Jika masalah menyangkut save yang tidak bisa dimuat, sebutkan ukuran file dan pesan error, dan kami akan mencari cara berbagi yang aman.

Kerentanan atau bug yang bisa merusak save tanpa peringatan: laporkan lebih dulu lewat Issue bertanda `safety`
(atau kontak maintainer) dan hindari menyertakan langkah detail yang menyulitkan pengguna lain sebelum ada perbaikan.

## Gaya kode dan commit

- C++17, tanpa RTTI dan tanpa exception (sesuai `Makefile`).
- Ikuti gaya file di sekitarnya: indentasi 4 spasi, komentar dalam bahasa Inggris, teks UI dalam bahasa Indonesia.
- Hindari alokasi besar yang tidak perlu; target RAM aplikasi homebrew mode applet terbatas.
- Periksa semua nilai kembalian I/O (`fopen`, `fwrite`, `fclose`, `Result`) dan kembalikan pesan error yang jelas ke UI.
- Pesan commit singkat dan deskriptif, boleh bahasa Indonesia atau Inggris, contoh:
  - `fix(bank): pulihkan dari .bak jika file utama hilang`
  - `feat(ui): tambah pemilih profil pengguna`
  - `test(sav): tambah kasus save terpotong`

## Kontribusi dengan bantuan AI

Boleh memakai asisten AI, dengan syarat:
- Kamu memahami dan sudah menguji kode yang diajukan, dan mencantumkan bahwa AI dipakai jika sebagian besar PR dihasilkan olehnya.
- AI tidak boleh "mengarang" offset, key, atau ukuran. Semuanya harus diverifikasi ke PKHeX.
- Berikan `AI_CONTEXT.md` ke asisten kamu sebagai konteks proyek.

## Lisensi

Monolit dirilis di bawah **GPL-3.0** karena offset, key blok, enkripsi, dan tabel diambil dari PKHeX (GPL-3.0).
Dengan mengirim kontribusi, kamu setuju kontribusimu dirilis di bawah lisensi yang sama.
Jangan menyertakan kode atau data yang lisensinya tidak kompatibel dengan GPL-3.0, atau materi berhak cipta
(ROM, key konsol, aset game). Teks lengkap lisensi ada di [`LICENSE`](LICENSE).

## Disclaimer dan aturan merek dagang

Proyek ini **tidak berafiliasi, tidak disponsori, dan tidak didukung** oleh Nintendo, The Pokémon Company, Creatures Inc., atau GAME FREAK inc.
Pokémon dan nama game terkait adalah merek dagang pemiliknya.

- Jangan menambahkan logo, gambar, sprite, ikon, musik, atau aset resmi lain (termasuk di UI, ikon `.nro`, dan dokumentasi).
  UI memakai teks dan bentuk generik buatan sendiri.
- Jangan menjual Monolit atau menarik bayaran dengan memakai nama/merek Pokémon.
- Dengan memakai CFW/homebrew, pengguna melanggar ToS Nintendo dan menanggung risiko ban online. Jangan menulis dokumentasi yang
  menjanjikan "aman dari ban".
- Rilis `.nro` harus mengikuti [`RELEASING.md`](RELEASING.md) (source + `LICENSE` + `NOTICE.md`). Atribusi ada di [`NOTICE.md`](NOTICE.md).
