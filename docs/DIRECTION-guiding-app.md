# 方向确认：原生安卓导星程序（PHD2 算法内核 + TR-Stepper 驱动）

> 2026-11-11 确定。原型验证见 `prototype/guiding-prototype.html`（自检 17/17）。
> 本文是后续开发的架构基线，代码落地目标仓库为 TR-Stepper。

## 1. 项目定位

在 TR-Stepper 的 Android APK 内实现**原生导星**：相机 USB 直连、赤道仪 Wi-Fi 直连，
不依赖 Termux / proot / Xvfb / INDI / PHD2 可执行文件。

算法内核移植自 PHD2 v2.6.14（BSD-3-Clause），不做 GUI、不做 Event Server。

**已确认的边界**

| 做 | 不做 |
|---|---|
| 移植 PHD2 算法、星点检测、校准、极轴工具 | 移植 PHD2 GUI / wxWidgets / 配置对话框 |
| 自研相机驱动（Android USB Host + ZWO SDK） | 跑 proot 里的 Linux PHD2 |
| 自研赤道仪驱动（LX200 TCP / WS，直连固件） | INDI / mount-relay / Xvfb |
| Capacitor 插件 + 原生前台 Service | PHD2 Event Server JSON-RPC |

**仍未消除的风险**：算法可移植性已用原型证明；**ZWO Android SDK 的 USB 兼容性没有证明**，
是 M1 的生死门（见 §7）。

## 2. 分层架构

```
┌──────────────────────────── Capacitor WebView ────────────────────────────┐
│  现有 TR 控制面板 + 新增「导星」页（取景 / 星点 / RA-Dec 曲线 / PA 工具）   │
└───────────────────────────────┬───────────────────────────────────────────┘
                                │ Capacitor Plugin (JS ⇄ Kotlin)
┌───────────────────────────────▼───────────────────────────────────────────┐
│ GuideService（Android Foreground Service，熄屏继续）                      │
│   ├─ CameraDriver   USB Host → ZWO libASICamera2.so → 帧                  │
│   ├─ MountDriver    TCP 192.168.4.1:4030（:Mg / :Q# / :GR / :GD / :GAT）  │
│   │                 或 WS :81（status JSON / cor:）                       │
│   └─ GuideCore      C++17 NDK 静态库，纯算法，无 UI、无网络、无相机        │
└───────────────────────────────────────────────────────────────────────────┘
```

**GuideCore 只做纯计算**：主机把帧喂进去，把脉冲回调出去。
这样离线回放、单元测试、以后换相机/换挂载都互不影响。

```c
// guidecore/api.h（目标接口）
typedef struct { const uint16_t *data; int w, h, stride; int bitDepth; int64_t tsUs; } GcFrame;
typedef struct { double raPx, decPx, snr, hfd; int valid; } GcStar;
typedef struct { int raMs, decMs; } GcPulse;   // 正负号表示方向

typedef void (*GcPulseFn)(GcPulse pulse, void *user);

int  gc_init(const char *configJson);
int  gc_set_params(const char *json);
int  gc_submit_frame(const GcFrame *frame);
int  gc_start_calibration(void);      // 内部按 N/E/S/W 调 pulse 回调
int  gc_start_guide(void);
void gc_stop(void);
int  gc_get_status(char *jsonOut, int cap);   // 星点/RMS/状态/PA 结果
void gc_set_pulse_callback(GcPulseFn fn, void *user);
```

## 3. GuideCore 模块（PHD2 源码 → 处理方式）

| 模块 | PHD2 文件 | 处理 |
|---|---|---|
| 图像缓冲 | `usImage.{h,cpp}` (661) | 直接搬 |
| 图像统计/直方图 | `image_math.{h,cpp}` (1362) | 搬核心，删 FITS I/O |
| 星点剖面 | `star_profile.cpp` (398) | 直接搬 |
| 星点检测/质心 | `star.cpp` (1154) + `point.h` | 直接搬（wx 类型用 shim） |
| 导星算法 | `guide_algorithm_*`（identity/hysteresis/resistswitch/lowpass/lowpass2/zfilter） | 直接搬，删配置面板 |
| 高斯过程 | `guide_algorithm_gaussian_process` + `contributions/MPI_IS_gaussian_process` | 可选，最后 |
| 统计 | `guiding_stats.cpp` (693) | 直接搬（无 wx） |
| 校准状态机 | `guider.cpp` 的 calibration 段 | **重写**，只保留数学 |
| 多星 | `guider_multistar.cpp` (1518) | 第二阶段 |
| 回差补偿 | `backlash_comp.cpp` (1289) | 第二阶段 |
| 静态极轴 | `staticpa_toolwin.cpp`（几何 776~1150） | 抽数学，UI 重写（原型已验证） |
| 漂移法 | `drift_tool.cpp` + `UpdateScopeCoordinates` | 抽数学 + 重写交互 |
| 漂移极轴 | `polardrift_toolwin.cpp` `WatchDrift` | 抽数学（原型已验证） |
| 导星助手 | `guiding_assistant.cpp` (2047) | 抽测量/统计，UI 重写 |
| 参数管理 | `guide_algorithm.h` + `guider.h` GuideOptions | 换成 JSON/C 结构，算法侧保留 Get/SetParam 语义 |
| **不搬** | `guider.cpp` 主体、`phd*.cpp`、`event_server`、`configdialog`、`cam_*`、`scope_*` | — |

**wx 剥离层（shim，约 200~300 行）**
- `Debug` → Android log / 环形日志
- `wxString` → 只在 `Debug/Format` 处替换
- `wxRect/wxPoint/wxSize/wxMin/wxMax` → 4 个简单类型
- 配置存储 → JSON/SharedPreferences
- 线程/计时 → `std::thread` / `std::chrono`

## 4. 相机驱动（CameraDriver）

**目标**：多厂商统一的 `IFrameSource`，暴露 `open/start/stop/getFrame/setExposure/setGain/setROI/setBinning`。
GuideCore 只认 `GcFrame`，不认识任何厂商 SDK。

### Android 的现实约束

Android 应用**不能直接打开 `/dev/bus/usb`**：USB 权限由 Java 的 `UsbManager` 授予，
native 侧拿不到 fd 就没有设备句柄。ZWO 的 Android SDK **没有提供"把 fd 传给 native"的 API**
（社区已确认），所以只能：

```
Kotlin：UsbManager 请求权限 → 调 SDK 的 Java 层打开相机 → 拿到 camera ID
C++   ：用这个 ID 调 ASIOpenCamera/ASIGetVideoData 读帧
```

即"**Java 负责 USB 开门，C++ 负责读帧**"。这不是把算法搬到 Java，GuideCore 仍然是纯 C++；
每帧只过一次 JNI（1~2 fps 的导星帧，开销可忽略）。若相机是 UVC，则 fd 可以正常传给
native（libusb/libuvc），这一限制不存在。

### 驱动分层

```
CameraDriver (Kotlin 接口：权限/打开/控制/热插拔)
 ├─ ZwoAsiDriver    Java 打开 + JNI 读帧（现有 ASI 相机）
 ├─ ToupcamDriver   libtoupcam.so（官方 Android SDK）
 ├─ UvcDriver       UsbManager fd → libuvc
 └─ SimDriver       合成帧 / 回放（无相机也能跑算法与前端）
        │
        ▼
Native IFrameSource → GcFrame → GuideCore
```

- 各厂商 SDK 的 `.so` 用 `dlopen` **运行时加载**，不参与主链接：
  缺哪个 SDK 就禁用哪个驱动，避免 ABI/RTTI 冲突互相拖累（ZWO 的 libstdc++ 尤其如此）。
- 帧统一成 16-bit 单色；掉线、超时、过热、USB 断开要有明确状态并自动重连。
- 热插拔：监听 `UsbManager` 广播，重新授权后恢复。
- 统一能力查询（曝光/增益/ROI/温度/制冷），驱动不支持的项在 UI 置灰。

### M1 若失败的回退顺序
1. 改用 ASICap APK 内的 `libASICamera2.so`
2. 用 `dlopen` + C 包装规避 libstdc++/RTTI 问题
3. 换 ToupTek/PlayerOne（官方 Android SDK）或 UVC 相机
4. 暂时保留"APK 遥控 Termux 内 PHD2"过渡方案（已有 `phdc.py` 基础）

## 5. 赤道仪驱动（MountDriver）

现有固件接口已成熟，**不需要 INDI、不需要 relay**：

| 功能 | 接口 |
|---|---|
| 脉冲导星 | TCP `:Mgn/:Mgs/:Mge/:Mgw<ms>#` |
| 中止 | `:Q#` |
| 位置/跟踪 | `:GR#`、`:GD#`、`:GAT#`、`:GVP#` |
| 导星速率 | `:Rg#` / `:Rg0.50#` / `:RG0/1/2#` |
| 状态 JSON | WebSocket `ws://<ip>:81` |
| 角秒纠正（备用） | WS `cor:±ra,±dec` |

工程要求：
- 独立 socket、`TCP_NODELAY`，测 RTT/抖动；脉冲时长误差要记录
- 断线重连 + 命令超时；禁止两条通道同时发脉冲（WS 与 TCP 二选一）
- 校准的 N/E/S/W 方向与 parity 必须用回放日志验证，符号错会"越导越偏"

## 6. 目录规划（落在 TR-Stepper）

```
software/tr-mount-apk/
├─ android/app/src/main/cpp/
│  ├─ guidecore/            # C++17，无 wx：image/ star/ algo/ calib/ pa/ stats/ params/
│  ├─ shim/                 # phd.h 最小替代、Debug、wx 类型
│  ├─ api/                  # extern "C" 接口
│  └─ jni/                  # JNI 桥
├─ android/app/src/main/java/com/tr/equatorial/
│  ├─ guide/GuideService.java
│  ├─ guide/CameraDriver.java
│  ├─ guide/MountDriver.java
│  └─ guide/GuidePlugin.java
└─ www/                     # 新增导星页 + canvas 图表
```

## 7. 里程碑与验收

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0** | `guidecore/` + shim + CMake NDK 骨架；离线回放接口 | arm64 `.so` 编过；合成帧质心误差 < 0.1 px |
| **M1（生死门）** | 最小 APK：USB 授权 → 打开 ASI → 显示一帧 | 640×480 连续出帧 ≥ 1 fps |
| M2 | 星点检测 + 导星页取景 + 曲线 | 实时 ≥ 1 fps，星点稳定锁定 |
| M3 | 校准 + `:Mg` 脉冲闭环 | 四方向脉冲位移方向/比例正确 |
| M4 | RA/Dec 导星环 + RMS/峰值/漂移率 | 模拟漂移下收敛；真机 RMS 合理 |
| M5 | 静态极轴 / 漂移法 / 漂移极轴 / 导星助手 | 与原型结果一致；真机可出修正量 |
| M6 | 多星、回差、PEC、日志导出 | 回归测试通过 |

**每个里程碑的验收都必须包含离线回放**：PHD2 的 GuideLog/星图序列喂给 `guidecore`，
输出与 PHD2 对比。这是"搬运不走样"的唯一证据。

## 8. 许可证

- PHD2 核心：BSD-3-Clause（部分相机头文件例外），可并入 GPLv3
- TR-Stepper：GPLv3
- 分发 APK 时保留 PHD2 版权声明与源码获取方式；新增 `NOTICE` 文件

## 9. 进度与下一步

**已完成（2026-11-11）**：M0 算法切片——`core/` 内 6 种导星算法 +
WindowedAxisStats + Bessel/Butterworth 滤波器工厂，40 项回归测试通过
（`cmake -S core -B core/build -G "MinGW Makefiles" && ctest --test-dir core/build`）。
另有可视化验收：`python core/tools/make_report.py` 生成自包含 HTML，
展示合成漂移下各算法的 RA/DEC 曲线、修正量与 RMS 对比。

星点层（`star.cpp` + `usImage` + `Median3`）已移植：`Star::Find` 质心/HFD/SNR
与 `GuideStar::AutoFind`（中值 → 降采样 → PSF 卷积 → 局部极大值 → 筛选）
均为 PHD2 原流程，仅把 `pFrame/pCamera/pGuider` 换成 `AutoFindParams`。
20 项测试通过；`python core/tools/make_star_report.py` 生成合成星场验收页
（检出位置/真值、主星放大质心、HFD、误差表）。

帧级导星循环也已接通：`guide_session.{h,cpp}` 移植自
`guider_multistar.cpp::UpdateCurrentPosition()` 的单星路径——首帧 `AutoFind` 锁星，
之后每帧从上次位置 `Find`，`star - lockPos` 作为相机偏移送进 RA/DEC 导星算法。
`python core/tools/make_replay_report.py` 用 300 帧真实渲染的星图跑闭环（赤道仪
1 帧延迟），展示残差、累计修正与 SNR/HFD；236 项检查通过。

校准数学层也已移植：`calibration.{h,cpp}` 含 `Calibration` 结构、相机↔赤道仪
坐标变换（含正交误差）、`MoveOffset` 的脉冲毫秒换算、赤纬补偿与子午线翻转、
West/North 轴求解；10242 项检查通过，其中包含 PHD2 `TestTransforms()` 的
全部正/反转角度组合。`GuideSession` 现在先做相机→赤道仪变换再把偏移送进算法，
校准有效时同时给出有符号脉冲毫秒。

校准状态机也已完成：`CalibrationSession` 按帧驱动，逐行移植
`Scope::UpdateCalibrationState` 的 West 测量 → East 回中 → 回差清除 → North 测量
→ South 回中 → nudge 回起点；合成赤道仪完整跑通并生成路径图
（`python core/tools/make_cal_report.py`）。整包 10273 项检查通过。
尚未移植的是 parity（需要 RA/Dec 回读）和通知/告警 UI。

导星回放已升级为完整链路：先跑校准，再用校准结果做相机→赤道仪变换、
导星算法和 1 ms 量化的脉冲输出，合成赤道仪按脉冲移动星点并保留 1 帧延迟。
`python core/tools/make_replay_report.py` 同时展示残差、累计修正和脉冲毫秒。

内核已封装成可交互形态：`python core/tools/guide_live_server.py` 起一个本地
页面（`guide_live` 进程按行接收命令，Python 标准库 HTTP 服务转发）。页面上
可以开始/暂停、切换算法、改 minMove/视宁度/漂移、扰动星点、重新校准、重置，
实时看到星场、残差、脉冲与滚动 RMS。这层只是把已有 `GuideSession` +
`CalibrationSession` 接上输入输出，没有新增算法逻辑，M1 之后可以原样换成
相机帧源和 `:Mg` 脉冲输出。

NDK 交叉编译已接通：`core/` 在 Android 构建下输出共享库
（`arm64-v8a libguidecore.so`，NDK r27c / Clang 18，ELF64 AArch64），
主机测试与仿真工具在 Android 构建下自动跳过。命令见 `core/README.md`。
算法内核到 APK 的剩余缺口只有 JNI/`gc_*` C 接口与相机、赤道仪驱动。

**前端设计（2026-11-11）**：NINA 风格的横屏导星 APK 首版设计已定稿，
覆盖全局侧栏、主页导星面板、可折叠扇形手控、设备参数、赤道仪页与导星页
（px/″ 切换、算法参数、校准向导）。首版用 SimCamera + SimMount 全模拟闭环，
不验证 M1。设计稿见 `docs/superpowers/specs/2026-11-11-nina-style-guiding-apk-design.md`。

**下一步**
1. **前端首版**：按设计稿在 TR-Stepper APK 落地，跑通模拟闭环
2. **M1 相机门**：搭最小 APK 验证 ZWO SDK（不可跳过）
3. **M0 续**：接真实帧序列/GuideLog 回放（帧接口已就绪）
3. M1 之后补 `gc_*` C 接口 + JNI，再动 M2 及以后
