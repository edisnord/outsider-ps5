#!/usr/bin/env bash
# Packs the built title folder into a release zip.
#
#   scripts/package.sh [VERSION]      VERSION defaults to the VERSION file
#
# Writes dist/outsider-ps5-v<VERSION>.zip (one top-level folder, PPSA77110, ready to copy to
# /data/homebrew) and dist/SHA256SUMS. Run scripts/build.sh first.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-$(tr -d '[:space:]' < "$REPO/VERSION")}"
TITLE_ID="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["titleId"])' "$REPO/sce_sys/param.json")"
SRC="$REPO/build/$TITLE_ID"
for f in eboot.bin outsider.bin outsider/shims sce_sys/param.json sce_sys/icon0.png LICENSE; do
    [ -e "$SRC/$f" ] || { echo "package.sh: $SRC/$f is missing; run scripts/build.sh first" >&2; exit 1; }
done
command -v zip >/dev/null || { echo "package.sh: zip is not installed" >&2; exit 1; }

REV="$(git -C "$REPO" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
OUTSIDER_REV="$(tr -d '[:space:]' < "$REPO/OUTSIDER_REV")"
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -a "$SRC" "$STAGE/$TITLE_ID"
cat > "$STAGE/$TITLE_ID/VERSION.txt" <<TXT
Outsider PS5 v$VERSION (early alpha)
outsider-ps5 revision: $REV
Outsider revision:     $OUTSIDER_REV (https://github.com/edisnord/outsider, branch jsc)
Built:                 $(date -u +%Y-%m-%dT%H:%M:%SZ)
TXT
cat > "$STAGE/$TITLE_ID/SOURCE.txt" <<TXT
The programs in this package are built from GPL-licensed source. The corresponding source is:
  outsider-ps5  https://github.com/edisnord/outsider-ps5  revision $REV (tag v$VERSION)
  Outsider      https://github.com/edisnord/outsider      revision $OUTSIDER_REV
and the libraries listed in THIRD-PARTY-NOTICES.md. Ask in the outsider-ps5 repository's issues if a piece is missing.
TXT

mkdir -p "$REPO/dist"
ZIP="$REPO/dist/outsider-ps5-v$VERSION.zip"
rm -f "$ZIP"
( cd "$STAGE" && zip -qr -9 "$ZIP" "$TITLE_ID" )
( cd "$REPO/dist" && sha256sum "$(basename "$ZIP")" > SHA256SUMS )
echo "wrote $ZIP ($(du -h "$ZIP" | cut -f1))"
cat "$REPO/dist/SHA256SUMS"
