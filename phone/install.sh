#!/usr/bin/env bash
# install.sh — 在 proot Ubuntu 24.04 里安装 PC 编好的 arm64 PHD2
#
# 用法（proot 里，本目录下）：
#   ./install.sh [/path/to/phd2-arm64.tar.gz]
set -euo pipefail

TAR="${1:-$(cd "$(dirname "$0")/../.." && pwd)/phd2-arm64.tar.gz}"
DEST=/opt

say() { printf '\033[1;36m[install]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[install] %s\033[0m\n' "$*" >&2; exit 1; }

[ -f "$TAR" ] || die "找不到 $TAR（从 PC 拷过来：scp pc:D:/Users/LEGLON-TR/Desktop/phd2build/phd2-arm64.tar.gz .）"
[ "$(uname -m)" = "aarch64" ] || die "需要 aarch64"

say "解包 $TAR → $DEST/phd2-dist"
tar -xzf "$TAR" -C "$DEST"
chmod +x "$DEST/phd2-dist/bin/"*

say "装运行时依赖（wx/OpenCV/INDI 桥/PTY）"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends socat indi-bin \
  libwxgtk3.2-1t64 libopencv-core406t64 libopencv-imgproc406t64 \
  libopencv-videoio406t64 libcurl4t64 libcfitsio10t64 libnova-0.16-0t64 libx11-6

# PHD2 自带的 wrapper 只加 lib/phd2，libusb（打过 termux patch）在 lib/ 下
cat > "$DEST/phd2-dist/bin/phd2" <<'WRAP'
#!/bin/sh
export LD_LIBRARY_PATH="/opt/phd2-dist/lib/phd2:/opt/phd2-dist/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$0.bin" "$@"
WRAP
chmod +x "$DEST/phd2-dist/bin/phd2"

say "安装启动脚本 → /root/phd2-run.sh"
install -m 755 "$(dirname "$0")/phd2-run.sh" /root/phd2-run.sh

say "自检"
MISSING=$(LD_LIBRARY_PATH="$DEST/phd2-dist/lib/phd2:$DEST/phd2-dist/lib" \
  ldd "$DEST/phd2-dist/bin/phd2.bin" | grep -c "not found" || true)
say "缺失动态库数量: $MISSING（0 才能启动）"
LD_LIBRARY_PATH="$DEST/phd2-dist/lib" "$DEST/phd2-dist/bin/usbprobe" || true
ls -l "$DEST/phd2-dist/bin"
say "完成。相机插上后在 Termux 原生侧跑 termux-launch.sh"
