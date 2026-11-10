#!/bin/bash
# Build PHD2 + patched libusb for arm64 inside qemu-user chroot on WSL
set -euo pipefail
exec > >(tee -a /var/log/phd2-stage2.log) 2>&1
echo "=== START $(date) ==="

ROOT=/opt/arm64-noble
SRC=/opt/phd2-src
PREFIX=/opt/phd2-dist
# WSL 是 NAT：Windows 代理要开「允许局域网连接」，WSL 才能用网关地址
# 用 PHD2_PROXY 覆盖；传空字符串表示不用代理（WSL 已开 mirrored 网络时）
PROXY="${PHD2_PROXY-http://172.21.0.1:7897}"
if [ -n "$PROXY" ]; then
  export http_proxy="$PROXY" https_proxy="$PROXY" HTTP_PROXY="$PROXY" HTTPS_PROXY="$PROXY"
fi
echo "== [0/7] connectivity check (proxy=${PROXY:-none}) =="
curl -s -o /dev/null -m 20 -w 'github=%{http_code}\n' https://github.com || true
curl -s -o /dev/null -m 20 -w 'mirror=%{http_code}\n' https://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports/dists/noble/Release || true

echo "== [1/7] mounts =="
mkdir -p /build-host
mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
mountpoint -q "$ROOT/sys" || mount -t sysfs sys "$ROOT/sys"
mountpoint -q "$ROOT/dev" || mount --bind /dev "$ROOT/dev"
mountpoint -q "$ROOT/build" || { mkdir -p "$ROOT/build"; mount --bind /build-host "$ROOT/build"; }

echo "== [2/7] fetch PHD2 source =="
# 优先用手机稀疏克隆后甩过来的精简源码（541MB 全量仓库经代理要 4 小时）
PHONE_TAR=/mnt/d/Users/LEGLON-TR/Desktop/phd2build/phd2-src.tar.gz
if [ ! -f /opt/phd2-src.tar.gz ] && [ -f "$PHONE_TAR" ]; then
  cp "$PHONE_TAR" /opt/phd2-src.tar.gz
fi
if [ ! -f /opt/phd2-src.tar.gz ]; then
  # 兜底：单请求 tarball（git clone 经代理常被 TLS 掐断）
  curl -L --fail --retry 5 --retry-delay 3 -o /opt/phd2-src.tar.gz.tmp \
    https://codeload.github.com/OpenPHDGuiding/phd2/tar.gz/refs/tags/v2.6.14 \
  || curl -L --fail --retry 5 --retry-delay 3 -o /opt/phd2-src.tar.gz.tmp \
    https://ghfast.top/https://github.com/OpenPHDGuiding/phd2/archive/refs/tags/v2.6.14.tar.gz
  mv /opt/phd2-src.tar.gz.tmp /opt/phd2-src.tar.gz
fi
if [ ! -f "$SRC/CMakeLists.txt" ]; then
  mkdir -p "$SRC"
  tar -xzf /opt/phd2-src.tar.gz -C "$SRC" --strip-components=1
fi
mkdir -p /build-host
if [ ! -e /build-host/phd2 ]; then cp -a "$SRC" /build-host/phd2; fi

echo "== [3/7] fetch libusb 1.0.27 + termux patch =="
mkdir -p "$ROOT/opt/src"
if [ ! -f "$ROOT/opt/src/libusb-1.0.27.tar.bz2" ]; then
  curl -L --fail -o "$ROOT/opt/src/libusb-1.0.27.tar.bz2" \
    https://github.com/libusb/libusb/archive/refs/tags/v1.0.27.tar.gz
fi
if [ ! -f "$ROOT/opt/src/termux-usb-support.patch" ]; then
  curl -L --fail -o "$ROOT/opt/src/termux-usb-support.patch" \
    https://raw.githubusercontent.com/termux/termux-packages/master/packages/libusb/termux-usb-support.patch
fi

echo "== [4/7] apt deps in chroot =="
chroot "$ROOT" /bin/bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends build-essential cmake pkg-config git ca-certificates \
  libwxgtk3.2-dev wx-common gettext zlib1g-dev libx11-dev libcurl4-openssl-dev \
  libopencv-dev libeigen3-dev libgtest-dev libindi-dev libnova-dev libusb-1.0-0-dev \
  libudev-dev liblzma-dev libtiff-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev
'

echo "== [5/7] build patched libusb =="
chroot "$ROOT" /bin/bash -c '
set -e
cd /opt/src
if [ ! -d libusb-1.0.27 ]; then
  mkdir -p libusb-1.0.27
  tar xf libusb-1.0.27.tar.bz2 -C libusb-1.0.27 --strip-components=1
  cd libusb-1.0.27
  patch -p1 < ../termux-usb-support.patch
else
  cd libusb-1.0.27
fi
if [ ! -f Makefile ]; then
  ./configure --prefix=/usr/local
fi
make -j8
make install
ldconfig
'

echo "== [6/7] configure + build PHD2 =="
chroot "$ROOT" /bin/bash -c '
set -e
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
cd /build/phd2
mkdir -p tmp
cd tmp
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_SYSTEM_LIBUSB=1 \
  -DCMAKE_INSTALL_PREFIX='"$PREFIX"' \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition"
make -j8
make install
'

echo "== [7/7] probes + package =="
chroot "$ROOT" /bin/bash -c '
set -e
mkdir -p /opt/src
cat > /opt/src/usbprobe.c <<EOF
#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
int main(void){
  libusb_context *ctx=NULL; libusb_device **list=NULL;
  const char *fd=getenv("TERMUX_USB_FD");
  int r=libusb_init(&ctx);
  printf("libusb_init=%d TERMUX_USB_FD=%s\n", r, fd?fd:"(unset)");
  if(r) return 1;
  ssize_t n=libusb_get_device_list(ctx,&list);
  printf("devices=%zd\n", n);
  for(ssize_t i=0;i<n;i++){
    struct libusb_device_descriptor d;
    if(libusb_get_device_descriptor(list[i],&d)==0)
      printf("  %04x:%04x bus=%d addr=%d\n", d.idVendor,d.idProduct,
        libusb_get_bus_number(list[i]), libusb_get_device_address(list[i]));
  }
  libusb_free_device_list(list,1);
  libusb_exit(ctx);
  return 0;
}
EOF
cat > /opt/src/asi_probe.c <<EOF
#include <stdio.h>
#include <ASICamera2.h>
int main(void){
  int n=ASIGetNumOfConnectedCameras();
  printf("ASI cameras=%d\n", n);
  for(int i=0;i<n;i++){
    ASI_CAMERA_INFO info;
    if(ASIGetCameraProperty(&info,i)==ASI_SUCCESS)
      printf("  [%d] %s id=%d\n", i, info.Name, info.CameraID);
  }
  return 0;
}
EOF
gcc /opt/src/usbprobe.c -o '"$PREFIX"'/bin/usbprobe -I/usr/local/include -L/usr/local/lib -lusb-1.0 -pthread \
  || echo "WARN: usbprobe build failed"
gcc /opt/src/asi_probe.c -o '"$PREFIX"'/bin/asi_probe \
  -I/build/phd2/cameras/zwolibs/include \
  /build/phd2/cameras/zwolibs/linux/armv8/libASICamera2.a \
  -L/usr/local/lib -lusb-1.0 -lstdc++ -lm -lpthread -ldl -lrt \
  || { echo "WARN: asi_probe build failed"; ls -l /build/phd2/cameras/zwolibs/linux/armv8/ /build/phd2/cameras/zwolibs/include/ || true; }
mkdir -p '"$PREFIX"'/lib
cp -a /usr/local/lib/libusb-1.0.so* '"$PREFIX"'/lib/
file '"$PREFIX"'/bin/phd2 '"$PREFIX"'/bin/usbprobe '"$PREFIX"'/bin/asi_probe || true
'

tar -C "$ROOT/opt" -czf /opt/phd2-arm64.tar.gz phd2-dist
ls -lh /opt/phd2-arm64.tar.gz
cp -f /opt/phd2-arm64.tar.gz /mnt/d/Users/LEGLON-TR/Desktop/phd2build/ 2>/dev/null \
  && echo "artifact copied to D:\\Users\\LEGLON-TR\\Desktop\\phd2build\\" || true
echo "=== DONE $(date) ==="
