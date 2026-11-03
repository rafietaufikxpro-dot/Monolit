#!/usr/bin/env bash
# Builds a GPL-3.0 compliant release package: Monolit.nro + matching source + LICENSE + NOTICE.
# Usage: tools/release.sh            (needs devkitPro, see README)
#        SKIP_BUILD=1 tools/release.sh   (reuse an existing Monolit.nro)
set -euo pipefail
cd "$(dirname "$0")/.."

VER=$(sed -n 's/^APP_VERSION *:= *//p' Makefile)
[ -n "$VER" ] || { echo "cannot read APP_VERSION from Makefile" >&2; exit 1; }
NAME="Monolit-v$VER"
OUT="dist/$NAME"

if [ -z "${SKIP_BUILD:-}" ]; then
    make -f Makefile.host test
    make clean && make
fi
[ -f Monolit.nro ] || { echo "Monolit.nro not found" >&2; exit 1; }

rm -rf "$OUT" && mkdir -p "$OUT"
cp Monolit.nro LICENSE NOTICE.md README.md "$OUT/"

# Source archive that corresponds to this exact binary (GPL-3.0 section 6).
if git rev-parse --git-dir >/dev/null 2>&1; then
    [ -z "$(git status --porcelain)" ] || { echo "WARNING: uncommitted changes - source archive will not match the working tree" >&2; }
    git archive --format=tar.gz --prefix="$NAME-src/" -o "$OUT/$NAME-src.tar.gz" HEAD
    echo "Source commit: $(git rev-parse HEAD)" > "$OUT/SOURCE.txt"
else
    tar --exclude=./build --exclude=./dist --exclude='*.nro' --exclude='*.elf' --exclude='*.nacp' --exclude='*.bank' --exclude='*.bak' \
        --transform "s,^\.,$NAME-src," -czf "$OUT/$NAME-src.tar.gz" .
    echo "Source: $NAME-src.tar.gz (no git metadata)" > "$OUT/SOURCE.txt"
fi

(cd "$OUT" && sha256sum * > SHA256SUMS.txt)
(cd dist && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME")
echo "Done: dist/$NAME.zip"
echo "Reminder: put the source link, GPL-3.0 notice and the Nintendo ToS/ban warning in the release notes (see RELEASING.md)."
