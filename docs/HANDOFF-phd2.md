# PHD2 on Phone — PC 端接手说明书

> **2026-11-10 整理：项目根目录已合并为 `D:\Users\LEGLON-TR\Desktop\TRXARI\`。**
> 本文里的 `phd2build\` 对应现在的 `artifacts\`、`pc-build\`；
> `PHD2-PC-HANDOFF\fix\` 对应 `phone\`。后续在此目录工作。

> 生成于 2026-11-10。目标：不 root、不用 mini PC，让一加 Ace5（Termux + proot Ubuntu 24.04）
> 独立跑 PHD2 导星：ASI 相机走 USB，赤道仪走 Wi-Fi（LX200 / TCP 4030）。
> 手机侧脚本已完成，**剩下的全在 PC 上：下载源码 → qemu chroot 编 arm64 → 产物回手机**。

---

## 0. 一句话现状

| 阶段 | 状态 |
|---|---|
| 方案验证（不需要 ASCOM，PHD2 自带 ZWO armv8 静态库） | ✅ |
| 手机侧脚本（启动/运行/安装/USB 探针） | ✅ 已写，见手机仓库 `software/phd2-phone/` |
| 手机侧 Termux:X11（命令 + APK） | ✅ 已装 |
| PC 侧 arm64 noble chroot | ✅ 已建：`/opt/arm64-noble`（`uname -m` = aarch64） |
| PHD2 源码下载 | ✅ 194 MB tarball（直连，无需代理） |
| 编 arm64 PHD2 | ✅ **完成 2026-11-10 18:32** |
| 装进手机 proot、跑起来 | ✅ 2026-11-10 夜（PHD2+indiserver+Xvfb 均在跑） |
| 赤道仪链路（跟踪 + 脉冲导星） | ✅ 2026-11-10 22:4x 直连实测 |
| 真相机导星 | ⬜ 缺 ASI 相机（现在只有 PHD2 内置 Simulator） |

### 2026-11-10 深夜实测结论（重要）

1. **之前那轮"导星成功"无效**：相机是 PHD2 自带的 Simulator（`asi_probe`=0 台，
   `TERMUX_USB_FD` 从未传进 proot）。模拟星场不跟赤道仪走，校准 `xRate≈1e-4`、
   `ortho_error=87.7°`，RA/Dec 必然一直报 "not able to make sufficient corrections"。
2. **赤道仪已单独验证通过**（绕过 PHD2，直连 `192.168.4.1:4030`）：
   - `:GVP#` → `AM TR-Stepper#`
   - `:Te#` → `1#`，`:GAT#` → `1#`（开跟踪后 RA 从"随 LST 走"变冻结）
   - `:Mgw10000#`（0.5×，10 s）→ RA 精确 +5 s ⇒ **脉冲导星通**
   - 赤道仪在 home 位（轴角≈0，Dec 读数 +89°59'59"），室内只能测到这一步。
3. **坑：PHD2/INDI 连上不会自动开跟踪**。INDI 里 `TRACK_SIDEREAL=On` 只是属性，
   挂载端实测 `:GAT#=0`。正式导星前必须先发 `:Te#`（或用 Alpaca/SkySafari 打开跟踪），
   否则校准和导星全废。
4. **修掉的 bug**：`proot-distro login` 用 `env -i` 起，**不转发 `TERMUX_USB_FD`**，
   原来 `termux-launch.sh` / `probe-usb.sh` 的相机 fd 根本到不了 libusb。
   改为先写 `$TMPDIR/usbfd`（`--shared-tmp` → proot 里 `/tmp/usbfd`），
   进 proot 后 `export TERMUX_USB_FD=$(cat /tmp/usbfd)`。两个脚本已更新到手机仓库
   `software/phd2-phone/`（git 里是 M 状态，未提交）；假 fd 验证 `INNER=42` 通过。

---

## 2026-11-10 23:4x 协议切换：Standard LX200 → OnStep

- INDI 驱动由 `indi_lx200generic`（设备名 `Standard LX200`）换成
  **`indi_lx200_OnStep`**（设备名 `LX200 OnStep`，`indi-bin` 自带，无需新装）。
  `phd2-run.sh` 里可覆盖：`SCOPE_DRIVER=indi_lx200_OnStep`，设备名改成自动探测。
- PHD2 实测：`Connected Scope:INDI Mount [LX200 OnStep]`，camera/mount 均 connected。
- **PHD2 配置里有两个键，改一个不够**：
  - `[profile/1/scope] LastMenuChoice=INDI Mount [LX200 OnStep]` ← 只是菜单显示
  - `[profile/1/indi] INDImount=LX200 OnStep` ← **真正连接用的设备名**
- PHD2 退出时会重写配置文件：改 profile 必须先把 PHD2 **彻底停掉**
  （`kill -9`，否则 SIGTERM 会让它把旧值写回去）。
- `pkill -f mount-relay.py` 在 proot 里会匹配到自己的命令行（脚本正文在 proot 的
  cmdline 里）→ 改用 `pkill -f "^python3 /root/mount-relay.py"` 或在 Termux 侧只按 PID 杀。
- Termux:X11 应用被切到后台后 X server 会挂死（xdotool 超时）→ 需要重启 `termux-x11`；
  验证逻辑尽量走无头 Xvfb:99，不依赖手机前台。

---

## 相机到手后的验证顺序（更新）

```bash
# Termux 原生
cd ~/mobile/TR-Stepper/software/phd2-phone
./probe-usb.sh          # 三段全绿：termux-usb -l / usbprobe / asi_probe
./termux-launch.sh      # 无头起 PHD2；加 DEBUG_X11=1 可看手机上的窗口
```

PHD2 里相机选 **ZWO ASI Camera**、赤道仪选 **INDI Mount**（localhost:7624）→
**确认跟踪已开**（`:GAT#`=1）→ 指向真星 → 校准 → 导星。

> 产物：`D:\Users\LEGLON-TR\Desktop\phd2build\phd2-arm64.tar.gz`（44 MB，
> md5 `355352eafe64f0e59f478f4711ca5d3b`）。含 `bin/phd2`、`phd2.bin`(19 MB aarch64)、
> `usbprobe`、`asi_probe`、`lib/libusb-1.0.so*`（已打 TERMUX_USB_FD patch）、
> `lib/phd2/*.so`（toupcam/ogmacam/SVB/PlayerOne）。
>
> 本机踩坑（已修进 `phd2-stage2.sh`）：代理 172.21.0.1:7897 不通且 TUNA 镜像直连 403
> → 全部直连 + 华为云 apt 源；libusb 必须用 release 资产（源码归档无 configure）；
> termux patch 的 `<bits/fcntl.h>` 在 glibc 下要删；OGMA SDK 和 INDI 2.1.6 的 git clone
> 在 qemu 里会挂死 → 都用 tarball 预取；系统 libindi 1.9.9 头文件必须屏蔽（API 不同）；
> `usbprobe` 的 include 是 `<libusb-1.0/libusb.h>`。
>
> `install.sh` 已补运行时依赖（wx3.2 / OpenCV / libcurl / cfitsio / libnova）并重写
> wrapper 让 `LD_LIBRARY_PATH` 含 `lib/`（libusb 在那里）。

---

## 1. 先确认梯子活着

```cmd
curl -x http://127.0.0.1:7897 -s -o NUL -w "%{http_code}" https://github.com
```
必须出 `200`。出 `000` 说明加速软件没起来 / 端口不对（之前踩过：客户端崩了，系统代理还留在 7897）。

**并且打开客户端的「允许局域网连接 / Allow LAN」** —— WSL2 是 NAT 网络，不打开的话 WSL 连不到
`127.0.0.1:7897`，只能用 Windows 网关 `172.21.0.1:7897`。

---

## 2. 下载源码（在 Windows 上做，比 WSL 快且省事）

全量 tarball 有 ~400 MB（大头是别的相机的厂商 .so）。既然梯子通了，直接整个下来最省心：

```cmd
curl.exe -L -x http://127.0.0.1:7897 ^
  -o D:\Users\LEGLON-TR\Desktop\phd2build\phd2-src.tar.gz ^
  https://codeload.github.com/OpenPHDGuiding/phd2/tar.gz/refs/tags/v2.6.14
```

下完确认大小（应 > 200 MB）：
```cmd
dir D:\Users\LEGLON-TR\Desktop\phd2build\phd2-src.tar.gz
```

> 慢的话备选：稀疏拉取（只 ~45 MB）。在 WSL 里：
> ```bash
> git -c http.version=HTTP/1.1 clone --depth 1 --filter=blob:none --no-checkout \
>   --branch v2.6.14 https://github.com/OpenPHDGuiding/phd2.git /tmp/phd2src
> cd /tmp/phd2src
> git sparse-checkout set --no-cone '/*' '!/locale' '!/help' '!/WinLibs' \
>   '!/cameras/toupcam' '!/cameras/altair' '!/cameras/qccdlibs' '!/cameras/qhyccdlibs' \
>   '!/cameras/svblibs' '!/cameras/zwolibs/mac' '!/cameras/zwolibs/windows' \
>   '!/cameras/zwolibs/linux/x64' '!/cameras/zwolibs/linux/x86' \
>   '!/cameras/zwolibs/linux/armv7' '!/cameras/zwolibs/linux/armv6'
> git checkout v2.6.14
> tar -czf /mnt/d/Users/LEGLON-TR/Desktop/phd2build/phd2-src.tar.gz --exclude=.git .
> ```
> 注意：上次在手机上跑这套只落了 3.6 MB（网络中途断），务必确认 `ls cameras/zwolibs/linux/armv8/`
> 有 `libASICamera2.a`、根目录有 `CMakeLists.txt` 再打包。

---

## 3. 编译（WSL 里跑，qemu-user 模拟 arm64）

```cmd
ssh 连不连都行，直接在 PC 上开 WSL：
wsl -d Ubuntu -u root -e bash /mnt/d/Users/LEGLON-TR/Desktop/phd2build/phd2-stage2.sh
```

脚本 `phd2-stage2.sh` 做这些事（7 步，全自动）：
1. 挂 proc/sys/dev，把 `/build-host` bind 到 chroot 的 `/build`
2. 取源码：**优先用 `D:\Users\LEGLON-TR\Desktop\phd2build\phd2-src.tar.gz`**，没有才自己去下
3. 取 libusb 1.0.27 + Termux 的 `termux-usb-support.patch`
4. chroot 里 `apt install` 依赖（wx3.2、libindi、opencv、libusb…）
5. 编打过 patch 的 libusb → `/usr/local`
6. `cmake -DUSE_SYSTEM_LIBUSB=1 -DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-multiple-definition` 编 PHD2
7. 编两个探针并打包：`/opt/phd2-arm64.tar.gz`（含 `phd2`、`usbprobe`、`asi_probe`、libusb .so）

**代理**：脚本默认 `PHD2_PROXY=http://172.21.0.1:7897`（Windows 网关）。
- 已开 mirrored 网络 / 直接用 Windows 网络时：`PHD2_PROXY= bash phd2-stage2.sh`
- 端口不是 7897：`PHD2_PROXY=http://172.21.0.1:<端口> bash phd2-stage2.sh`
- 脚本第一步会打印 `github=200 mirror=200`，先看这个再等编译

产物会自动复制到 `D:\Users\LEGLON-TR\Desktop\phd2build\phd2-arm64.tar.gz`。
日志：`/var/log/phd2-stage2.log`。

> qemu 用户态编 C++ 很慢（几十分钟到几小时）。中途 Ctrl-C 后重跑不会重装依赖，
> cmake 也不会重配，会快很多。

---

## 4. 产物回手机

手机自己来拉（手机能 ssh 到 PC，PC 反过来不行）：

```bash
# 手机 Termux
scp pc:D:/Users/LEGLON-TR/Desktop/phd2build/phd2-arm64.tar.gz /root/termux-home/
```

然后进 proot 安装：

```bash
proot-distro login ubuntu
cd /root/termux-home/mobile/TR-Stepper/software/phd2-phone
./install.sh /root/termux-home/phd2-arm64.tar.gz
```

`install.sh` 会：解包到 `/opt/phd2-dist` → 装 `socat` + `indi-bin` → 装 `/root/phd2-run.sh`
→ 跑一次 `phd2 --version` 自检。

---

## 5. 手机侧怎么用（已写好，见仓库）

| 文件 | 跑在哪 | 作用 |
|---|---|---|
| `termux-launch.sh` | Termux 原生 | 找 ZWO(vid `03c3`) → `termux-usb -r` 授权 → 起 termux-x11 → 进 proot |
| `phd2-run.sh` | proot | `socat` 把 `192.168.4.1:4030` 接到 PTY → `indiserver indi_lx200generic` → 起 PHD2 |
| `probe-usb.sh` | Termux 原生 | 相机到手先跑：`termux-usb -l` + `usbprobe` + `asi_probe` |
| `install.sh` | proot | 解包 + 装依赖 |

相机到手后的验证顺序：`probe-usb.sh` 三段全绿 → PHD2 向导里
相机选 **ZWO ASI Camera**、赤道仪选 **INDI Mount**（`localhost:7624`）→ 出图 → 校准 → 导星。

---

## 6. 已知坑（都踩过，别再试）

1. **`pkg` / `apt` 在 proot 里不能跑**：Termux 打了 root 补丁，`getuid()==0` 直接退出。
   嵌套 `proot -i <uid>` 能骗过检查，但 apt 会卡死在 `ptrace_stop`。
   → 装 Termux 包只能直接拉 deb + `dpkg-deb -x`（Termux:X11 就是这么装的）。
2. **`termux-usb`、boot 脚本只能在 Termux 原生跑**，proot 里调不通。
3. **PHD2 是 GUI**，必须有 X。Termux:X11 已装：`termux-x11 :0 -ac` + Termux:X11 应用。
4. **`ssh pc` 默认是 cmd.exe**：`/dev/null` 这种路径会报「系统找不到指定的路径」，
   bash 脚本要过 Git Bash 或直接 `wsl -d Ubuntu -u root -e bash <脚本>`。
5. **`scp` 到 Windows 路径会静默失败**：务必 `scp -v` 或事后 `grep` 验证文件真的更新了
   （之前因为静默失败，拿旧脚本跑了好几轮）。
6. **Ubuntu 26.04 没有 `qemu-user-static`**，改成了 `qemu-user-binfmt`。
7. **PHD2 全量仓库 541 MB**，其中 400 MB 是别的相机厂商二进制，能不全下就别全下。

---

## 7. 文件清单（本文件夹）

```
D:\Users\LEGLON-TR\Desktop\phd2build\
├─ HANDOFF-phd2.md        ← 本文件
├─ phd2-stage1.sh         ← 建 arm64 noble chroot（已完成，重跑无害）
├─ phd2-stage2.sh         ← 下载 + 编译 + 打包（待跑）
├─ phd2-src.tar.gz        ← 需要你下（第 2 步）
└─ phd2-arm64.tar.gz      ← stage2 产物
```

手机侧脚本在仓库 `software/phd2-phone/`，手机已是最新。
