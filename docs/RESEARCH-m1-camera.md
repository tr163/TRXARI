# M1 调研：安卓原生相机连接

> 2026-11-11。目标：APK 内通过 USB OTG 直接打开天文导星相机，不依赖 Termux/proot。
> 结论先行：**可行，但 ZWO 官方 Android SDK 是"能用但脆弱"；ToupTek 是更干净的备选；
> 必须先做最小 APK 实测，不能只靠文档判断。**

## 0. 三种路线对比

| 路线 | 官方 Android 支持 | 风险 | 适用 |
|---|---|---|---|
| **ZWO ASI Android SDK** | 有，但长期不更新；v1.39 仍在被要求补 Android 二进制 | 高：USB 权限模型、libstdc++/RTTI、16KB page | 已有 ZWO 相机时 |
| **ToupTek Android SDK** | 有：`libtoupcam.so`，arm64-v8a 等 4 个 ABI，NDK r18b 编译，带 Java 示例 | 中低 | 新购相机时优先考虑 |
| **UVC（libusb/libuvc）** | Android 标准 USB Video Class | 低 | 轻量备选；长曝光/导星能力受限 |
| proot + Linux PHD2 | — | 已验证但重 | 仅过渡回退 |

## 1. ZWO ASI Android SDK（主路线）

**获取**：<https://www.zwoastro.com/software/> → `OTHERS` → `For Developers`。
没有直链；论坛要求每个 SDK 版本同时更新 Android 二进制（v1.39 的发布帖里仍在提）。

**已知问题（按严重度）**

1. **USB 权限模型**
   - 早期官方 Android SDK 不支持 Android 10/11 的新权限模型；社区把 `libASICamera2.so`、
     `libusb-1.0.so`、`libzwo_camera.so` 换成 **ASICap.apk 里抽出来的版本**才工作。
   - 后来 ZWO 在论坛帖里给了更新文件， reportedly 修复了权限问题。
   - **没有官方 API 把 Android `UsbManager` 的 fd 传给 native**。可行路径是：
     Java 侧用 SDK 的 Java API（`ZwoCamera` 等）打开相机拿 camera ID，再把 ID 传进 native
     `ASIOpenCamera(id)`。即"Java open，native read"。
2. **C++ 运行时冲突**
   - `libASICamera2.so` 链的是 `libstdc++.so`，现代 NDK 用 `libc++`；
     `dynamic_cast`/异常跨边界会崩。规避：**不跨边界用 RTTI/异常，`dlopen` 动态加载**。
3. **16KB page size**
   - ZWO 论坛已有人指出 `libASICamera2.so`/`libusb-1.0.so`/`libzwo_camera.so` 不支持 16KB page。
   - Android 15 起部分设备/商店要求；目前侧载 + 4KB page 机型（含一加 Ace5）可用，
     但这是长期隐患，升级 Android/上架前要复查。
4. **SDK 不开源**：不能用官方源码重编，只能等 ZWO 更新或从 ASICap 抽二进制。

**参考实现：OpenLiveStacker（GPLv3）**
- <https://github.com/artyom-beilis/OpenLiveStacker>
- 架构几乎就是我们要的：C++ 后端 + 浏览器前端（`http://127.0.0.1:8080`），
  Android 上直接支持 ZWO ASI、UVC、ToupTek。
- ASI 驱动：`src/asi_camera.cpp`（约 630 行，`ASIGetCameraPropertyByID`/`ASIOpenCamera`/
  `ASIGetVideoData`，封装很薄）。
- 作者就是踩遍 ZWO Android SDK 坑的人；其结论可当作 M1 的已知答案。
- 许可证 GPLv3 与 TR-Stepper 一致，代码可参考/复用（保留版权）。
- 注意：其 Android USB 权限/Activity 代码不在主仓（主仓有 `src/android_main.cpp`），
  早期 UVC 版在 `artyom-beilis/android_live_stacker`；正式 APK 的 Java 层需再找或从其 APK 反查。

## 2. ToupTek / PlayerOne / SVBony（备选）

- **ToupTek**：官方多平台 SDK 明确含 Android，`libtoupcam.so` 提供 arm/arm64/x86/x64，
  用 NDK r18b 编译；包内有 `demosimplest`（约 60 行）和 Java 示例。
  <https://www.touptekphotonics.com.cn/download/?dlID=6>
- **PlayerOne**：ToupTek StellaVita 的兼容列表写"PlayerOne 全部设备"，
  说明其协议/SDK 与 ToupTek 同源；但**买之前必须确认型号能用 ToupTek SDK 直接打开**。
- **SVBony**：StellaVita 只列 SC715C；SV105/SV205 走 UVC。官方 Android SDK 未确认。
- 结论：**如果要新买导星相机，ToupTek/PlayerOne 的 Android 官方支持比 ZWO 干净得多。**

## 3. UVC 路线（最省事，但有上限）

- Android 标准 USB Video Class，`libuvc`/`libusb` 即可；OpenLiveStacker 已实现。
- 优点：不依赖任何厂商 SDK，USB 权限标准化。
- 缺点：很多 UVC 相机的曝光上限/增益/温度控制不完整，长曝光导星未必够用；
  需要实测具体型号（如 SV205）。

## 4. 一加 Ace5 的平台事实

- Android 15 / ColorOS 15；USB-C 为 **USB 2.0**，支持 OTG。
- ColorOS 的 OTG 开关在 **设置 → 辅助功能 → OTG 连接**；默认可能关闭。
- **OTG 会在约 10 分钟无活动后自动关闭** —— 导星过程中必须保持 USB 活动并做断线重连。
- 相机功耗大时可能需要带供电的 OTG 分线器。

## 5. M1 实测计划（建议顺序）

### 第 0 步：拿到 SDK 并体检
1. 从 ZWO `For Developers` 下载最新 SDK，记录版本号。
2. 解包 Android 目录，检查：
   - ABI 是否含 `arm64-v8a`
   - `.so` 是否 16KB page 对齐（`llvm-readelf -l`）
   - Java 类名/包名（`ZwoCamera`？）和 JNI 库
   - 是否自带 `device_filter.xml` / USB 权限示例
3. 解包 `ASICap.apk` 备份其中的三个 `.so`，作为已知可用二进制备胎。

### 第 1 步：最小 APK（不接导星核心）
- 一个 Activity：`UsbManager` 枚举 → 请求权限 → 打开相机 → 取一帧 → `ImageView` 显示。
- 记录：打开耗时、帧率、断线重连、前后台切换。
- 真机：一加 Ace5，OTG 已开，必要时带供电。

### 第 2 步：判定
| 结果 | 动作 |
|---|---|
| 能稳定出帧 ≥1 fps | M1 通过，进入 M0/M2 |
| 只有 ASICap 的 .so 能跑 | 锁定该版本二进制，继续 |
| 需要 Java API 打开 | 接受"Java open + native read"，调整架构 |
| 完全打不开 | 评估 ToupTek/UVC 换相机，或临时回退 proot |

### 第 3 步：稳定性
- 拔插重连、熄屏/亮屏、USB 供电不足、连续 30 分钟出帧。
- 记录 16KB page 与 Android 15 的兼容表现。

## 6. 需要你确认的信息

1. **现有相机型号**（ASI120MM Mini / ASI220MM Mini / 其他？）—— 决定 SDK 兼容性优先级。
2. 是否接受"必要时更换为 ToupTek/PlayerOne 相机"。
3. 是否接受最终通过 **Java/Kotlin 打开相机、C++ 读帧**（这是 ZWO Android 的现实路径）。

## 7. 来源

- ZWO Android SDK 问题与权限：<https://bbs.zwoastro.com/d/16038-asi-zwo-android-sdk-critical-bugs/5>
- Android SDK 讨论 / fd 传递：<https://bbs.zwoastro.com/d/15678-android-sdk/7>
- 16KB page size：<https://bbs.zwoastro.com/d/25021-android-sdk-and-16-kb-memory-page-size/4>
- ASI SDK v1.39：<https://bbs.zwoastro.com/d/24861-asi-sdk-v139-has-been-released>
- Cloudy Nights 更新讨论：<https://www.cloudynights.com/forums/topic/870616-interesting-updates-about-asi-zwo-android-sdk/>
- OpenLiveStacker：<https://github.com/artyom-beilis/OpenLiveStacker>
- ToupTek Android SDK：<https://www.touptekphotonics.com.cn/download/?dlID=6>
- StellaVita 兼容列表：<https://www.touptek-astro.com/StellaVita/compatibility/>
