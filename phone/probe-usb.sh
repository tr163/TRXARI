#!/data/data/com.termux/files/usr/bin/bash
# probe-usb.sh — Phase 0：相机到手后第一件事，验 USB 链路
#
# 不依赖 PHD2，只回答三个问题：
#   1. Linux 认到设备了吗（termux-usb -l）
#   2. 打过 patch 的 libusb 能不能打开 fd（usbprobe）
#   3. ZWO SDK 能不能数到相机（asi_probe）
set -uo pipefail

say() { printf '\033[1;36m[probe]\033[0m %s\n' "$*"; }

say "== 1. termux-usb -l =="
termux-usb -l 2>&1 || say "termux-usb 失败（Termux:API 应用/权限？）"

say "== 2. ZWO 设备筛选（vid 03c3） =="
D="$(termux-usb -l 2>/dev/null | tr '{' '\n' | grep -i 03c3 | head -1 \
     | sed -n 's/.*"device"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
if [ -z "$D" ]; then
  say "没找到 ZWO 设备 —— 换数据线 / 确认相机供电"
  exit 1
fi
say "设备 = $D"

say "== 3. libusb + ASI SDK（授权后进 proot） =="
# proot-distro login 是 env -i 起的，不转发 TERMUX_USB_FD → 走 --shared-tmp 的 /tmp。
termux-usb -r -e "printf %s \"\$TERMUX_USB_FD\" > \$TMPDIR/usbfd; proot-distro login ubuntu --shared-tmp -- bash -lc '
  export TERMUX_USB_FD=\$(cat /tmp/usbfd)
  export LD_LIBRARY_PATH=/opt/phd2-dist/lib:/usr/local/lib:\$LD_LIBRARY_PATH
  echo \"TERMUX_USB_FD=\$TERMUX_USB_FD\"
  /opt/phd2-dist/bin/usbprobe
  /opt/phd2-dist/bin/asi_probe
'" "$D"
say "完成。三项都过 → 进 Phase 1（PHD2 认相机）"
