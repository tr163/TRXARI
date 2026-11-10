#!/usr/bin/env bash
# phd2-run.sh — 在 proot Ubuntu 里跑：INDI LX200 桥 + PHD2
set -euo pipefail

MOUNT_HOST="${MOUNT_HOST:-192.168.4.1}"
MOUNT_PORT="${MOUNT_PORT:-4030}"
RELAY_PORT="${RELAY_PORT:-4032}"
PHD2_DIST="${PHD2_DIST:-/opt/phd2-dist}"
SCOPE_DRIVER="${SCOPE_DRIVER:-indi_lx200_OnStep}"

say() { printf '\033[1;36m[phd2]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[phd2]\033[0m %s\n' "$*" >&2; exit 1; }

[ -x "$PHD2_DIST/bin/phd2" ] || die "找不到 $PHD2_DIST/bin/phd2（先跑 install.sh）"
[ -n "${TERMUX_USB_FD:-}" ] && say "相机 fd = $TERMUX_USB_FD" || say "没有 TERMUX_USB_FD（无相机模式）"

export LD_LIBRARY_PATH="$PHD2_DIST/lib:/usr/local/lib:${LD_LIBRARY_PATH:-}"
export PATH="$PHD2_DIST/bin:$PATH"
export LIBUSB_NO_DEVICE_SCAN="${LIBUSB_NO_DEVICE_SCAN:-1}"

if [ -z "${DISPLAY:-}" ] && [ "${USE_XVFB:-1}" = "1" ]; then
  command -v Xvfb >/dev/null || die "需要 Xvfb"
  Xvfb :99 -screen 0 1280x800x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
  XVFB_PID=$!
  sleep 1
  export DISPLAY=:99
  say "Xvfb :99 已启动（无头）"
fi

# ---- 赤道仪：本机 relay + INDI OnStep 驱动 ----
# relay 负责两件事：预连上游（躲开 9s 的首次 SYN 丢包）、代答 INDI 的裸 0x06 握手。
if [ "${NO_MOUNT:-0}" != "1" ]; then
  command -v indiserver >/dev/null || die "需要 indi-bin"
  command -v python3 >/dev/null || die "需要 python3"
  pkill -x indiserver 2>/dev/null || true
  pkill -f "^python3 /root/mount-relay.py" 2>/dev/null || true
  sleep 1

  python3 /root/mount-relay.py "$RELAY_PORT" "$MOUNT_HOST" "$MOUNT_PORT" >/tmp/relay.log 2>&1 &
  RELAY_PID=$!
  ping -i 5 -q "$MOUNT_HOST" >/dev/null 2>&1 &
  PING_PID=$!

  for _ in $(seq 1 30); do
    grep -q "listening 127.0.0.1:$RELAY_PORT" /tmp/relay.log 2>/dev/null && break
    sleep 1
  done

  indiserver -v "$SCOPE_DRIVER" >/tmp/indiserver.log 2>&1 &
  INDI_PID=$!
  trap 'kill $INDI_PID $RELAY_PID $PING_PID 2>/dev/null || true; kill ${XVFB_PID:-} 2>/dev/null || true' EXIT

  # 设备名由驱动决定（generic = "Standard LX200"，OnStep = "LX200 OnStep"），动态探测。
  DEV=""
  for _ in $(seq 1 15); do
    DEV=$(timeout 4 indi_getprop 2>/dev/null | sed -n 's/^\([^.]*\)\..*/\1/p' | head -1)
    [ -n "$DEV" ] && break
    sleep 1
  done
  [ -n "$DEV" ] || die "indiserver 起来了但没发现设备（看 /tmp/indiserver.log）"
  say "INDI 设备：$DEV（驱动 $SCOPE_DRIVER）"

  indi_setprop "$DEV.CONNECTION_MODE.CONNECTION_TCP=On" >/dev/null 2>&1 || true
  sleep 1
  indi_setprop "$DEV.DEVICE_ADDRESS.ADDRESS=127.0.0.1" >/dev/null 2>&1 || true
  indi_setprop "$DEV.DEVICE_ADDRESS.PORT=$RELAY_PORT" >/dev/null 2>&1 || true
  sleep 1

  MOUNT_OK=0
  indi_setprop "$DEV.CONNECTION.CONNECT=On" >/dev/null 2>&1 || true
  for _ in $(seq 1 15); do
    sleep 2
    if timeout 4 indi_getprop 2>/dev/null | grep -q "^$DEV.CONNECTION.CONNECT=On"; then
      MOUNT_OK=1; break
    fi
  done

  if [ "$MOUNT_OK" = "1" ]; then
    say "赤道仪已连上 $MOUNT_HOST:$MOUNT_PORT（$DEV，本地中继）"
    # 确保跟踪已开（OnStep 驱动在 home 位不一定自己发 :Te#）
    indi_setprop "$DEV.TELESCOPE_TRACK_MODE.TRACK_SIDEREAL=On" >/dev/null 2>&1 || true
  else
    say "警告：赤道仪没连上 $MOUNT_HOST:$MOUNT_PORT（看 /tmp/relay.log /tmp/indiserver.log）"
  fi
fi

say "启动 PHD2（DISPLAY=${DISPLAY:-none}）"
exec "$PHD2_DIST/bin/phd2" "$@"
