# Panduan rilis (kepatuhan GPL-3.0)

Cara termudah: jalankan `tools/release.sh` (tes, build, lalu paket `dist/Monolit-vX.Y.Z.zip` berisi `.nro`, arsip source, `LICENSE`, `NOTICE.md`, `SHA256SUMS.txt`). Commit semua perubahan dulu dan beri tag git, supaya source cocok dengan biner.

Setiap kali mendistribusikan `Monolit.nro` (GitHub Release, forum, Discord, dll.) wajib:

1. **Sertakan source yang sama persis** dengan biner: tag git (mis. `v0.2.0`) dan arsip source (`git archive`) di halaman rilis yang sama.
2. **Sertakan `LICENSE` dan `NOTICE.md`** di paket rilis (zip).
3. **Tulis di catatan rilis**: lisensi GPL-3.0, tautan source, dan versi devkitPro/libnx yang dipakai untuk build.
4. Jangan menambah pembatasan tambahan di luar GPL-3.0 (mis. "dilarang dibagikan ulang") pada biner atau source.
5. Jangan menyertakan save, ROM, `prod.keys`, atau aset resmi dalam paket.
6. **Jangan dijual.** Tidak ada ads/donasi yang mengaitkan proyek dengan merek Pokémon. Gratis saja paling aman terhadap pemegang merek.
7. Salin peringatan risiko ban (README, bagian "Peringatan penting") ke catatan rilis.

Contoh paket: `Monolit-v0.2.0.zip` → `Monolit.nro`, `LICENSE`, `NOTICE.md`, `README.md`, tautan ke source.
