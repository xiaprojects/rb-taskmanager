#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-RB-Commercial
# mkdeb.sh PKG STAGE_DIR VERSION OUT_DIR
# Turns a `make install DESTDIR=STAGE_DIR` tree into OUT_DIR/PKG_VERSION_ARCH.deb using
# the templates in packaging/PKG/ (control, maintainer scripts, conffiles).
set -eu

PKG=$1
STAGE=$(realpath "$2")
VERSION=$3
OUT=$(realpath -m "$4")
HERE=$(dirname "$(realpath "$0")")
TPL="$HERE/$PKG"
ARCH=$(dpkg --print-architecture)

[ -f "$TPL/control" ] || { echo "mkdeb: no template $TPL/control" >&2; exit 1; }
[ -d "$STAGE" ] || { echo "mkdeb: no stage dir $STAGE" >&2; exit 1; }

# Docs required by Debian policy
install -d "$STAGE/usr/share/doc/$PKG"
install -m 0644 "$HERE/copyright" "$STAGE/usr/share/doc/$PKG/copyright"
MAINT=$(sed -n 's/^Maintainer: //p' "$TPL/control")
{
    printf '%s (%s) unstable; urgency=medium\n\n' "$PKG" "$VERSION"
    if git -C "$HERE" rev-parse >/dev/null 2>&1; then
        git -C "$HERE" log -n 20 --format='  * %s' -- ..
    else
        printf '  * Release %s\n' "$VERSION"
    fi
    printf '\n -- %s  %s\n' "$MAINT" "$(date -R)"
} | gzip -n9 > "$STAGE/usr/share/doc/$PKG/changelog.gz"
chmod 0644 "$STAGE/usr/share/doc/$PKG/changelog.gz"

# Shared-library dependencies of every ELF file in the stage
ELFS=$(find "$STAGE" -type f -perm -u+x -exec sh -c 'head -c4 "$1" | grep -q "ELF"' _ {} \; -print)
SHLIBS=""
if [ -n "$ELFS" ]; then
    TMP=$(mktemp -d)
    mkdir "$TMP/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$PKG" "$PKG" > "$TMP/debian/control"
    # shellcheck disable=SC2086
    SHLIBS=$(cd "$TMP" && dpkg-shlibdeps -O $ELFS 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
    rm -r -- "$TMP"
fi
[ -n "$SHLIBS" ] || SHLIBS="libc6"

install -d -m 0755 "$STAGE/DEBIAN"
# Normalise permissions regardless of the builder's umask
find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -path "$STAGE/DEBIAN" -prune -o -type f ! -perm -u+x -exec chmod 0644 {} +
SIZE=$(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)
sed -e "s|@VERSION@|$VERSION|" -e "s|@ARCH@|$ARCH|" \
    -e "s|@SHLIBS@|$SHLIBS|" -e "s|@SIZE@|$SIZE|" \
    "$TPL/control" > "$STAGE/DEBIAN/control"

for f in preinst postinst prerm postrm; do
    [ -f "$TPL/$f" ] && install -m 0755 "$TPL/$f" "$STAGE/DEBIAN/$f"
done
[ -f "$TPL/conffiles" ] && install -m 0644 "$TPL/conffiles" "$STAGE/DEBIAN/conffiles"

(cd "$STAGE" && find . -path ./DEBIAN -prune -o -type f -printf '%P\0' \
    | xargs -0 -r md5sum > DEBIAN/md5sums)

install -d "$OUT"
DEB="$OUT/${PKG}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$DEB"
echo "Built $DEB"
