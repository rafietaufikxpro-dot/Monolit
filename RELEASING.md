# Release guide (GPL-3.0 compliance)

Easiest way: run `tools/release.sh`. It runs the tests, builds, and creates `dist/Monolit-vX.Y.Z.zip` containing the `.nro`,
a source archive, `LICENSE`, `NOTICE.md` and `SHA256SUMS.txt`. Commit all changes and create a git tag first so the source
matches the binary.

Whenever you distribute `Monolit.nro` (GitHub Release, forum, Discord, etc.) you must:

1. **Include the exact source of the binary:** a git tag (e.g. `v0.2.0`) and a source archive (`git archive`) on the same release page.
2. **Include `LICENSE` and `NOTICE.md`** in the release package (zip).
3. **State in the release notes:** the GPL-3.0 license, the source link, and the devkitPro/libnx versions used to build.
4. Do not add restrictions beyond GPL-3.0 (for example "no redistribution") to the binary or the source.
5. Do not include saves, ROMs, `prod.keys` or official assets in the package.
6. **Do not sell it.** No ads or donations that tie the project to the Pokémon brand. Keeping it free is the safest option with respect to trademark holders.
7. Copy the ban-risk warning (README, "Important warnings" section) into the release notes.

Example package: `Monolit-v0.2.0.zip` → `Monolit.nro`, `LICENSE`, `NOTICE.md`, `README.md`, link to the source.
