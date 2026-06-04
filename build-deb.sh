#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

PKG_NAME="daima-agent"
VERSION="${VERSION:-0.1.1}"
ARCH="${ARCH:-amd64}"
BUILD_ROOT="$SCRIPT_DIR/build-deb"
PKG_ROOT="$BUILD_ROOT/${PKG_NAME}_${VERSION}_${ARCH}"
OUT_DIR="$SCRIPT_DIR/dist"

echo "=== Building daima ==="
./build.sh system

echo "=== Staging Debian package ==="
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/DEBIAN" \
         "$PKG_ROOT/usr/bin" \
         "$PKG_ROOT/usr/lib/daima/bin" \
         "$PKG_ROOT/usr/share/daima-agent/spiffs_data"

install -m755 "$SCRIPT_DIR/build-host/daima" "$PKG_ROOT/usr/lib/daima/bin/daima"
cp -a "$SCRIPT_DIR/spiffs_data/." "$PKG_ROOT/usr/share/daima-agent/spiffs_data/"
install -m755 "$SCRIPT_DIR/packaging/deb/usr-bin-daima.in" "$PKG_ROOT/usr/bin/daima"
install -m755 "$SCRIPT_DIR/packaging/deb/init-home.sh.in" "$PKG_ROOT/usr/lib/daima/init-home.sh"

cat > "$PKG_ROOT/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libc6, libcurl4, libssl3, python3
Maintainer: Daima <daima@localhost>
Description: Daima local AI agent
 Daima host agent with Web UI, skills, cron, file tools, and optional Vector robot channel.
EOF

cat > "$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v systemctl >/dev/null 2>&1; then
    systemctl stop daima >/dev/null 2>&1 || true
    systemctl disable daima >/dev/null 2>&1 || true
    systemctl daemon-reload || true
fi
echo "Daima installed. Run foreground: daima"
echo "Per-user configuration will be initialized at ~/.daima on first run."
exit 0
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postinst"

find "$PKG_ROOT" -type d -exec chmod 0755 {} +
find "$PKG_ROOT/usr/share/daima-agent/spiffs_data" -type f -exec chmod 0644 {} +
chmod 0755 "$PKG_ROOT/usr/bin/daima" \
           "$PKG_ROOT/usr/lib/daima/bin/daima" \
           "$PKG_ROOT/usr/lib/daima/init-home.sh"

mkdir -p "$OUT_DIR"
dpkg-deb --build "$PKG_ROOT" "$OUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo "=== Package built: $OUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb ==="
