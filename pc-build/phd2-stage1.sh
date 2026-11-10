#!/bin/bash
set -euo pipefail
exec > >(tee -a /var/log/phd2-stage1.log) 2>&1
echo "=== START $(date) ==="
export DEBIAN_FRONTEND=noninteractive
export http_proxy="http://100.125.217.14:8888"
export https_proxy="$http_proxy"
export HTTP_PROXY="$http_proxy"
export HTTPS_PROXY="$http_proxy"

ROOT=/opt/arm64-noble
PORTS=https://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/
HOSTMIRROR=https://mirrors.tuna.tsinghua.edu.cn/ubuntu/

echo "== [1/5] host apt sources -> tuna https =="
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
  mv /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources.disabled
fi
cat > /etc/apt/sources.list <<EOF
deb $HOSTMIRROR resolute main universe restricted multiverse
deb $HOSTMIRROR resolute-updates main universe restricted multiverse
deb $HOSTMIRROR resolute-security main universe restricted multiverse
EOF

echo "== [2/5] host packages =="
apt-get update
apt-get install -y debootstrap qemu-user-binfmt ca-certificates curl wget git
apt-get install -y binfmt-support || echo "binfmt-support unavailable (ok on resolute)"

echo "== [3/5] binfmt status =="
if ! ls /proc/sys/fs/binfmt_misc/ 2>/dev/null | grep -qi qemu; then
  echo "registering qemu binfmt manually"
  mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true
  systemctl restart systemd-binfmt 2>/dev/null || true
fi
ls /proc/sys/fs/binfmt_misc/ | grep -i qemu || echo "NO_QEMU_BINFMT"

echo "== [4/5] debootstrap arm64 noble =="
if [ ! -x "$ROOT/bin/bash" ]; then
  debootstrap --arch=arm64 --variant=minbase --include=ca-certificates noble "$ROOT" "$PORTS"
else
  echo "already exists: $ROOT"
fi

cat > "$ROOT/etc/apt/sources.list" <<EOF
deb $PORTS noble main universe multiverse restricted
deb $PORTS noble-updates main universe multiverse restricted
deb $PORTS noble-security main universe multiverse restricted
EOF

echo "== [5/5] chroot test =="
chroot "$ROOT" /bin/bash -c 'uname -m; grep VERSION_ID /etc/os-release'
echo "STAGE1_DONE $(date)"
