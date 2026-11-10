# TRXARI — PHD2 on Phone 项目总目录

一加 Ace5（Termux + proot Ubuntu 24.04，无 root）独立跑 PHD2 2.6.14 导星：
ZWO 导星相机走 USB OTG，赤道仪走 Wi-Fi LX200（TCP 4030，TR-Stepper）。

**当前状态（2026-11-10）**

| 项 | 状态 |
|---|---|
| PHD2 2.6.14 aarch64 编译 + 装进 proot | ✅ |
| INDI OnStep 驱动 + 本机 relay 连赤道仪 | ✅ 已实测 `INDI Mount [LX200 OnStep]` connected |
| 赤道仪跟踪 / 脉冲导星（`:Te#` / `:Mgw#`） | ✅ 直连实测 |
| PHD2 + Simulator 相机出图、Looping | ✅ |
| 真相机（ZWO ASI）导星 / 校准 / 极轴 | ⬜ **缺相机，未验证** |

> 注意：之前看到的“导星成功”是 PHD2 内置 Simulator 的假象，不是真星点。

## 目录

```
TRXARI/
├─ README.md                  ← 本文件（先看这个）
├─ docs/
│  ├─ HANDOFF-phd2.md         ← 完整交接说明（编译、安装、验证、踩坑）
│  ├─ HANDOFF-phd2-build-original.md  ← 早期 PC 端构建版（历史参考）
│  └─ session-recovery/       ← 被误删后恢复的会话记录
├─ phone/                     ← 装到手机上的脚本（Termux / proot）
│  ├─ termux-launch.sh        ← Termux 原生：USB 授权 → 进 proot 起 PHD2
│  ├─ probe-usb.sh            ← 相机到手先跑：三段 USB 自检
│  ├─ phd2-run.sh             ← proot：relay + indiserver(OnStep) + PHD2
│  ├─ mount-relay.py          ← 赤道仪 TCP 中继（预连 + 代答 ENQ 握手）
│  ├─ install.sh              ← 解包 PHD2 产物到 /opt/phd2-dist
│  └─ fixprofile.py / fixprofile2.py  ← 改 PHD2 配置里的赤道仪设备名
├─ pc-build/                  ← WSL 里编 arm64（stage1 建 chroot，stage2 编译打包）
├─ debug/                     ← LX200 直连探针、PHD2 事件服务器小工具
├─ screenshots/               ← OnStep 切换后的实测截图
└─ artifacts/
   └─ phd2-arm64.tar.gz       ← 编译产物（45 MB，md5 355352eafe64f0e59f478f4711ca5d3b）
```

## 相机到手后的下一步

```bash
# 手机 Termux 原生
cd ~/mobile/TR-Stepper/software/phd2-phone
./probe-usb.sh        # 三段全绿：termux-usb -l / usbprobe / asi_probe
./termux-launch.sh    # 起 PHD2；加 DEBUG_X11=1 可在手机上看到窗口
```

PHD2 里相机选 **ZWO ASI Camera**、赤道仪选 **INDI Mount**（localhost:7624）→
确认跟踪已开（`:GAT#`=1）→ 对真星 → 校准 → 导星。

## 三条硬规则（踩过的坑）

1. 改 PHD2 配置前必须 `kill -9`，否则退出时会把旧值写回去。
2. 实连设备名是 `[profile/1/indi] INDImount`，不是菜单项 `LastMenuChoice`。
3. `proot-distro login` 是 `env -i`，`TERMUX_USB_FD` 要走 `$TMPDIR/usbfd` 中转。

细节全在 `docs/HANDOFF-phd2.md`。
