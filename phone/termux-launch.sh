#!/data/data/com.termux/files/usr/bin/bash
# termux-launch.sh — 在 Termux 原生侧跑，负责 USB 授权并把控制权交给 proot
#
# 用法：
#   ./termux-launch.sh            # 自动找 ZWO（vid 03c3）
#   ./termux-launch.sh /dev/bus/usb/001/004
#   CAM_ONLY=1 ./termux-launch.sh # 只测相机连通，不起 PHD2
set -euo pipefail

DISTRO="${DISTRO:-ubuntu}"
RUNNER="${RUNNER:-/root/phd2-run.sh}"

say() { printf '\033[1;36m[launch]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[launch]\033[0m %s\n' "$*" >&2; exit 1; }

command -v termux-usb >/dev/null || die "缺 termux-usb —— 装 Termux:API 应用 + pkg install termux-api"

DEV="${1:-}"
if [ -z "$DEV" ]; then
  say "枚举 USB 设备（找 ZWO vid 03c3）"
  # termux-usb -l 输出 JSON 数组，一行一个设备；用 grep 抓视频类/ZWO
  LIST="$(termux-usb -l 2>/dev/null || true)"
  [ -n "$LIST" ] || die "termux-usb -l 没输出（Termux:API 应用装了吗？）"
  DEV="$(printf '%s\n' "$LIST" | tr '{' '\n' | grep -i '03c3' | head -1 \
          | sed -n 's/.*"device"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
  if [ -z "$DEV" ]; then
    say "没发现 ZWO 设备。当前列表："
    printf '%s\n' "$LIST"
    die "相机插了吗？换根数据线（要能传数据的，不是充电线）"
  fi
fi
say "目标设备：$DEV"

# proot-distro login 是 env -i 起的，不转发 TERMUX_USB_FD。
# 先写进 --shared-tmp 的 /tmp，再在 proot 里读回来。
FD_PASS='printf %s "$TERMUX_USB_FD" > $TMPDIR/usbfd; '
FD_READ='export TERMUX_USB_FD=$(cat /tmp/usbfd); '

if [ "${CAM_ONLY:-0}" = "1" ]; then
  say "CAM_ONLY：只跑 usbprobe + asi_probe"
  exec termux-usb -r -e "${FD_PASS}proot-distro login $DISTRO --shared-tmp -- bash -lc '${FD_READ}/opt/phd2-dist/bin/usbprobe; /opt/phd2-dist/bin/asi_probe'" "$DEV"
fi

# 默认无头：proot 里 phd2-run.sh 自己起 Xvfb，前端走 Event Server :4400。
# 想肉眼看 PHD2 窗口时加 DEBUG_X11=1，走 Termux:X11。
if [ "${DEBUG_X11:-0}" = "1" ] && [ -z "${DISPLAY:-}" ]; then
  if command -v termux-x11 >/dev/null; then
    say "起 Termux:X11 :0（调试模式）"
    termux-x11 :0 -ac >"${TMPDIR:-/tmp}/termux-x11.log" 2>&1 &
    sleep 2
    export DISPLAY=:0
    am start --user 0 -n com.termux.x11/.MainActivity >/dev/null 2>&1 || \
      say "请手动打开 Termux:X11 应用"
  else
    say "警告：没装 termux-x11（DEBUG_X11=1 才需要）"
  fi
fi
[ -n "${DISPLAY:-}" ] && say "DISPLAY=$DISPLAY" || say "无头模式（Xvfb + 4400）"

say "请求 USB 权限 → 进 proot 起 PHD2（授权弹窗请点允许）"
exec termux-usb -r -e "${FD_PASS}proot-distro login $DISTRO --shared-tmp -- bash -lc '${FD_READ}exec $RUNNER'" "$DEV"
