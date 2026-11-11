# 设计：NINA 风格导星 APK 前端（首版全模拟）

> 日期：2026-11-11
> 状态：视觉方案已获用户确认，本文为待审阅的文字稿
> 落地仓库：界面与 APK 在 TR-Stepper；算法内核在 TRXARI `core/`
> 设计参考：NINA（仅参考信息组织与行为，不复制代码）、PHD2 v2.6.14 算法与参数语义

## 1. 目标

在 TR-Stepper 的 Capacitor Android APK 中，做出一个横屏、NINA 式组织的导星控制前端：

- 用户能连接赤道仪与导星相机，运行校准、开始导星，看到预览、RA/DEC 曲线与误差统计。
- 算法使用 TRXARI 已移植的 PHD2 内核，不在前端重写导星数学。
- **首版使用全模拟（SimCamera + SimMount），不验证 M1 相机硬件**；闭环在 APK 内完整跑通，
  之后只替换驱动层即可接真设备。

## 2. 范围

### 首版（F1）做

| 页面/能力 | 内容 |
|---|---|
| 全局框架 | 横屏、左侧图标栏、TRX 字标、顶部状态栏、页面路由、状态持久化 |
| 主页 | 主相机空预览、常驻导星面板（1 Hz 动态曲线 + RMS/峰值统计）、可折叠扇形手控器 |
| 设备页 | 胶囊式连接/断开、导星与主成像参数、像素比例自动计算与保存 |
| 赤道仪页 | 状态、RA/DEC 角度、归零/停止/跟踪/速度、地理位置与校准信息 |
| 导星页 | 导星相机预览、RA/DEC 曲线（px ↔ ″）、算法与参数、校准流程与本次统计 |
| 原生闭环 | `GuideBridge` 插件 → JNI → `guidecore`；SimCamera + SimMount；状态 5 Hz、预览 1 fps |

### 首版不做

- 真实相机/赤道仪驱动（USB、ZWO SDK、LX200 TCP/WS 实连）；M1 按用户要求暂不验证。
- 校准页（PHD2 极轴工具）、GOTO 星图、设置页；侧边栏保留，页面显示「首版未开放」。
- 主相机预览、滤镜轮、Dither、多星、回差补偿、PEC、日志上传。
- PHD2 Event Server / INDI / proot / Xvfb。
- 前台 Service：首版引擎运行在插件进程内；接真实相机时再按方向文档补 `GuideService`。

## 3. 总体架构

```
┌──────────────────────── Capacitor WebView（TR-Stepper APK）────────────────────────┐
│  control_panel.html / control_panel.css / app.js / guide_view.js                  │
│  页面路由 · 手控 · 设备参数 · 曲线与统计                                            │
└───────────────┬──────────────────────────────────────────────┬─────────────────────┘
                │ Capacitor Plugin（JS ⇄ Kotlin）               │ 现有 WS :81（保留）
┌───────────────▼──────────────────────────────────────────────▼─────────────────────┐
│ GuideBridgePlugin.kt                                                                │
│   └─ libguide_bridge.so（JNI 薄层，TR-Stepper）                                     │
│        └─ libguidecore.so（TRXARI core：GuideSession / CalibrationSession / 算法）   │
│             ├─ SimCamera  合成星场，640×480，1 fps                                  │
│             └─ SimMount   接收手控/脉冲，积分漂移，给出 RA/DEC 角度                  │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

- **前端只消费 JSON 状态与 JPEG 帧**，不感知 C++ 对象生命周期。
- 首版模拟模式不启用 WS :81；现有 WebSocket 代码保留，作为后续真实赤道仪通道。
- 真实相机/赤道仪接入点：保持 `IFrameSource` / `IMount` 接口不变，替换 Sim 实现；
  UI 与 `guidecore` 不需要改动。

## 4. 全局界面框架

### 4.1 形态

- 强制横屏，参考画布 960×430 dp；适配到 800×360 dp 起，低于 400 dp 高度时允许页面内容纵向滚动。
- 深色主题；状态栏与导航栏使用沉浸式，处理安全区 inset。
- 页面切换不重建导星会话；切走再回来，曲线、预览、校准状态继续。

### 4.2 左侧图标栏（78 px）

从上到下：

1. **TRX 字标**——点击回主页（主页在栏内没有单独条目）。
2. 设备（设备管理与参数）
3. 赤道仪
4. 校准（首版占位）
5. GOTO（首版占位）
6. 导星
7. 设置（首版占位）
8. 底部：连接指示点 + 「n 台在线」

当前页用左侧 3 px 蓝色内阴影 + 深蓝底高亮。字标 SVG：

```html
<svg viewBox="0 0 60 20" aria-label="TRX">
  <g fill="none" stroke="#dfeaf8" stroke-width="2.1" stroke-linecap="square">
    <path d="M2 3h14M9 3v15"/>
    <path d="M22 3v15M22 3h6a4 4 0 0 1 0 8h-6M28 11l6 7"/>
    <path d="M40 3l16 15M56 3L40 18"/>
  </g>
</svg>
```

### 4.3 顶部状态栏（42 px）

- 左：当前页标题（如「主相机预览」「导星 · PHD2」「设备」）。
- 中：设备胶囊——赤道仪、主相机、导星相机，各带状态点与文字（在线/未连接/模拟）。
- 右：当前页主操作（主页「全部连接」、导星页「Loop / 校准 / 开始导星」等）。

### 4.4 配色与字形

| 用途 | 值 |
|---|---|
| 页面底 / 图标栏 | `#15151c` / `#0d0d14` |
| 面板 / 边框 | `#1a1a23` / `#2f2f3d` |
| 主文字 / 次文字 | `#d8d8e4` / `#85859a` |
| 强调蓝 / 选中底 | `#5b9bd5` / `#2b527e` |
| RA 曲线 / DEC 曲线 | `#5b9bd5` / `#d98c5f` |
| 警告 / 正常 | `#e0a030` / `#4caf50` |

- 数字统一 `font-variant-numeric: tabular-nums`，避免跳数时抖动。
- 可点击区域不小于 36×36 dp；扇形四键实际触控区向外扩 6 dp。

## 5. 页面设计

### 5.1 主页（点击 TRX 字标）

**主区域**：主相机实时预览。首版没有主相机，显示空态：

```
▣  未连接主相机
   连接主相机后，这里显示实时回传画面
```

**右侧常驻栏（212 px）——导星面板**：

- 标题「导星」+ LIVE 点（仅导星中显示）。
- 两行状态：状态（未连接/回放中/校准中/导星中/丢星）、当前 RA/DEC 算法名。
- 四格统计：总 RMS、峰值、RA RMS、DEC RMS。
- RA/DEC 滚动曲线：60 个样本（约 60 s），每秒从左侧推入一个新点；无数据时画基线并显示「—」。
- 图例与当前单位；下方注释「1 Hz 刷新 · 比例 x.xx″/px」。
- 单位跟随导星页的 px/″ 全局切换；默认 px。总 RMS 在另一单位下显示括号值（如 `0.42 px (1.63″)`）。

**可折叠手控器（浮在主预览右上角）**：

- 默认折叠为胶囊：`⌖ 手控 · 512× · ● 跟踪 ⌄`。折叠状态持久化。
- 点击胶囊展开为 **192 px 宽的镂空控件**：无卡片底色/边框，直接浮在画面上；
  仅保留 3 px 黑色投影保证可读，不遮挡背后预览。
- 展开态结构（同一 SVG 中心，同心）：
  - 中央四个方向楔形键（上/右/下/左），外半径 44、内半径 14，中心圆仅作视觉中心与「松开停止」反馈。
  - 左弧：速度滑块，与扇缘同心，对数刻度 **2×–2048×**，共 11 档：
    `2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048`。拖动时取最近档，数值实时显示；默认 512×。
  - 右弧：与左弧等大对称，仅两档拨动——上 ON / 下 OFF，松手后吸附并下发跟踪开关。
  - 右上角小 `⌃` 收起按钮。
- 手控语义：
  - 上/下 = DEC+ / DEC−，右/左 = RA+ / RA−。前端只发逻辑方向，半球与 parity 由赤道仪驱动处理。
  - 按住方向键持续移动，松手/`pointercancel`/移出立即停止。
  - 速度与跟踪状态持久化；连接后跟踪默认 OFF，由用户拨到 ON。

### 5.2 设备页

左右两栏（约 1.05 : 1）。

**左栏「设备连接」**：每台设备一行，含图标、名称、状态标签、副标题、右侧胶囊按钮。

| 设备 | 副标题（模拟） | 首版行为 |
|---|---|---|
| 赤道仪 | TR-Stepper · LX200 TCP · 192.168.4.1:4030 | 模拟连接，300 ms 后在线 |
| 导星相机 | ZWO ASI120MM Mini · 640×480 · 1.0 s | 模拟连接，在线后出帧 |
| 主相机 | 未选择型号 | 点击提示「首版未实现」 |
| 滤镜轮 | 未发现 | 点击提示「首版未实现」 |

- 胶囊按钮文字在「连接 / 断开」间切换；点击后先进入「连接中…」禁用态，再落定。
- 「全部连接」依次尝试四台；主相机与滤镜轮失败只提示，不影响已连设备。
- 底部说明：型号会自动填像元尺寸，未知型号可手改。

**右栏「设备参数」**：

| 分组 | 字段 | 默认 | 规则 |
|---|---|---|---|
| 导星系统 | 相机型号 | ZWO ASI120MM Mini | 下拉含 ASI120MM Mini / ASI220MM Mini / 模拟相机 / 自定义 |
|  | 像元尺寸 | 3.75 µm | 1–20 µm，可改 |
|  | 导星镜焦距 | 200 mm | 1–10000 mm |
|  | 像素比例 | 3.87 ″/px | 只读，自动计算 |
| 主成像系统 | 相机型号 | ZWO ASI533MC Pro | 同上风格 |
|  | 主镜焦距 | 585 mm | 1–10000 mm |
|  | 像素比例 | 1.33 ″/px | 只读 |

- 计算公式：`scale = 206.264806 × pixelSizeUm / focalMm`，显示 2 位小数。
- 型号选择自动填像元尺寸；选「自定义」后字段可编辑。
- 保存时校验：焦距与像元必须为正且在范围内；不合法则内联报错、不落盘。
- 保存到 `localStorage["trx.deviceParams.v1"]`，立即生效；导星页 px/″ 换算、主页统计同步更新。
- 首版参数只用于显示换算；引擎内部完全按 px 工作，不接收像素比例。

### 5.3 赤道仪页

**左栏**：

- 状态卡：连接状态、跟踪 ON/OFF、当前速度档、回差、固件版本（模拟值）。
- 轴角度：RA、DEC 两张大数字，每秒刷新；格式 `05h 34m 31s` / `+22° 00′ 52″`。
- 控制区（**不放完整手控器**，手控在主页）：
  - `归零位`、`停止`、`跟踪 ON/OFF`。
  - 速度：11 档对数选择器（与主页手控共用同一档位与持久化值）。
  - 动作结果通过状态栏 toast 反馈。

**右栏「地理与校准」**：

- 纬度 / 经度 / 时区 / 本机时间（1 Hz）/ 恒星时 LST。
- 极轴误差、最近校准时间、校准是否有效。
- 首版数据来自模拟配置，只读；GOTO 与设置页落地后再提供编辑入口。

### 5.4 导星页

顶部操作：`Loop`（切换）、`校准`、`开始导星 / 停止`。

内容为 2×2 面板：

1. **导星相机预览**（与曲线等大）
   - 640×480 JPEG，1 fps；叠加主星框、十字线。
   - 角标：`640×480 · Loop 1.0s · SNR 24.6`。
   - 下方四格：SNR/HFD、偏移 px、曝光、丢星计数。
2. **RA / DEC 曲线**（与预览等大）
   - 右上角分段按钮 `px | ″`，全局切换（主页统计与曲线同步）。
   - 60 点滚动窗口，1 Hz；RA 蓝、DEC 橙；图例右侧显示 `RMS RA / DEC` 与峰值。
   - 注释：内部仍按 px 导星，切换只改显示单位。
3. **导星算法与参数**
   - RA / DEC 各一个下拉：Identity、Hysteresis、ResistSwitch、Lowpass、Lowpass2、ZFilter。
   - 按所选算法显示参数：`minMove`（px）、`aggression`（%）、`hysteresis`（%），以及算法特有项；
     导星速率滑块 0.1–1.0×，默认 0.5×。
   - 滑块 `input` 时本地即时显示；停止拖动 150 ms 后调用 `setParams`，避免刷接口。
   - 参数保存到 `localStorage["trx.guideParams.v1"]`。
4. **校准 / 本次导星**
   - 六步进度：West → East → 回差 → North → South → 回中；当前步高亮，已完成步保留。
   - 统计：RMS RA、RMS DEC、峰值、平均脉冲。
   - 按钮：`Dither`（首版置灰）、`停止`、`清空校准`。
   - 未完成有效校准时，「开始导星」置灰，并提示「需要先完成校准」。

**状态约束**：

- Loop 与校准互斥；导星中禁止重新校准，需先停止。
- 丢星时曲线断开、脉冲停止；连续 5 帧丢星自动暂停导星并提示。
- 页面切走不断开会话；返回时立即拉取最新状态与一帧预览。

### 5.5 占位页

校准、GOTO、设置三页保留侧边栏入口，内容区显示：

```
◎ 首版未开放
   校准将在后续版本接入 PHD2 极轴工具
```

不允许空白死链；点击不报错。

> 术语区分：侧边栏「校准」是**极轴校准**（PHD2 PA 工具）；导星页里的校准是
> **四方向导星校准**，两者不是同一功能。

## 6. 数据与交互

### 6.1 导星状态机

```
disconnected → looping → calibrating → guiding ⇄ star_lost
                     └──────────── stop ─────────┘
```

- `looping`：出帧、锁星，不输出脉冲。
- `calibrating`：按帧推进 CalibrationSession；完成后回到 `looping`，`calibration.valid=true`。
- `guiding`：每帧算偏移 → 算法 → 有符号脉冲 → SimMount。
- `star_lost`：无有效星点；不积分 RMS。

### 6.2 状态与曲线

- 前端每 200 ms 调一次 `getStatus()`（5 Hz），只用于轻量显示；曲线每秒取一个样本。
- 曲线环形缓冲 60 点；清空校准/开始新会话时清空。
- 统计由引擎给出（与本会话脉冲/偏移一致），前端不自行重算 RMS，避免两套数字。
- 导星页与主页共用同一状态源；主页不重复发起第二路会话。

### 6.3 单位换算

- 所有引擎数值单位是 px。
- 显示角秒：`arcsec = px × pixelScale`；像素比例来自设备页。
- 单位选择存 `localStorage["trx.ui.v1"].unit`，默认 `px`。
- 设备参数变更后，已显示的角秒统计按新比例重算；历史曲线形状不变（线性缩放）。

### 6.4 本地持久化

| key | 内容 |
|---|---|
| `trx.ui.v1` | 当前页、手控展开、速度档、跟踪、单位 |
| `trx.deviceParams.v1` | 导星/主成像型号、像元、焦距 |
| `trx.guideParams.v1` | RA/DEC 算法与参数 |

Capacitor WebView 的 localStorage 在应用重启后保留；读取失败时用默认值，不阻塞启动。

## 7. 原生引擎与桥

### 7.1 C 接口

在 TRXARI `core/` 新增平台无关的引擎层（复用现有 `GuideSession`、`CalibrationSession`、算法与 Sim 驱动）：

```c
int32_t gc_engine_create(const char *configJson);
void    gc_engine_destroy(void);

int32_t gc_engine_start_loop(void);
int32_t gc_engine_stop_loop(void);
int32_t gc_engine_start_calibration(void);
int32_t gc_engine_start_guide(void);
int32_t gc_engine_stop(void);
int32_t gc_engine_clear_calibration(void);

int32_t gc_engine_set_params(const char *json);
int32_t gc_engine_set_tracking(int32_t on);
int32_t gc_engine_set_slew_rate(int32_t rate);
int32_t gc_engine_manual_move(int32_t axis, int32_t dir, int32_t on); // axis 0=RA 1=DEC
int32_t gc_engine_home(void);

// 返回 JSON 字节数；不足时返回所需长度
int32_t gc_engine_get_status(char *out, int32_t cap);
// 返回 JPEG 字节数；0 表示暂无新帧
int32_t gc_engine_get_preview(uint8_t *out, int32_t cap);
```

- 错误码统一负数；`get_status` 永不阻塞超过 20 ms。
- 引擎内部单工作线程按 1 Hz 出帧；状态快照用互斥锁拷贝，JNI 只读快照。
- 不使用 JNI 回调，避免线程附着与生命周期问题。
- 离线回放继续使用方向文档里的低层接口（`gc_init` / `gc_submit_frame` 等）；
  `gc_engine_*` 是应用内接 Sim 驱动、自动出帧的封装层，不替代低层接口。

### 7.2 状态 JSON

```json
{
  "state": "guiding",
  "frame": 1234,
  "star": { "valid": true, "snr": 24.6, "hfd": 2.4, "dx": 0.12, "dy": -0.08 },
  "guide": {
    "samples": 42, "rmsRa": 0.42, "rmsDec": 0.38, "rmsTotal": 0.40,
    "peak": 1.4, "lastRaMs": 12, "lastDecMs": -8
  },
  "calibration": {
    "valid": true, "step": "done", "angleDeg": 1.2,
    "raRate": 15.0, "decRate": 14.8, "orthoError": 0.5
  },
  "mount": { "tracking": true, "slewRate": 512, "raDeg": 123.4, "decDeg": 45.6 },
  "params": { "raAlgo": "hysteresis", "decAlgo": "resistswitch", "minMove": 0.2,
              "aggression": 70, "hysteresis": 10, "guideRate": 0.5 },
  "engine": { "coreVersion": "m0-20261111" }
}
```

- `calibration.step`：`idle | west | east | backlash | north | south | recenter | done | failed`。
- 丢星时 `star.valid=false`，`state="star_lost"`，`guide` 保留最后一次统计供显示。

### 7.3 模拟驱动

**SimCamera**

- 640×480 16-bit 单色，按 `exposureMs`（默认 1000）出帧。
- 星场随 SimMount 指向平移；主星亮度可配；叠加读出噪声与视宁度抖动。
- 使用固定种子的 PRNG 配置项，保证宿主测试可重复；UI 运行默认随机种子。

**SimMount**

- 初始 RA/DEC、赤纬与漂移率可配（默认 RA 0.8 px/s、DEC 0.3 px/s 的视觉漂移）。
- `tracking=ON` 时抵消 RA 漂移；`OFF` 时按恒星速率漂移。
- 手动移动按 11 档速度积分；`home` 归零；脉冲直接加到位置。
- 角度输出用于赤道仪页与状态栏。

### 7.4 Android 集成

- 新增 `GuideBridgePlugin`（`@CapacitorPlugin(name = "GuideBridge")`；沿用项目现有的 Java，不额外引入 Kotlin 工具链），方法：
  `startLoop/stopLoop/startCalibration/startGuiding/stop/clearCalibration/setParams/setTracking/setSlewRate/manualMove/home/getStatus/getPreview/destroy`。
- 新增 `libguide_bridge.so`：TR-Stepper 内的 JNI 薄层，编译时链接 `libguidecore.so`。
  源文件放在 `android/app/src/main/cpp/`，Kotlin 放在
  `android/app/src/main/java/com/tr/equatorial/guide/`。
- `libguidecore.so` 由 TRXARI `core/` 的 arm64 NDK 构建产出，通过脚本复制到
  `software/tr-mount-apk/android/app/src/main/jniLibs/arm64-v8a/`；同时复制公开头文件。
  脚本保证内核更新后可一键同步，不手工拷。
- `getPreview` 在 Kotlin 侧做 Base64 编码，JS 用 `<img src="data:image/jpeg;base64,...">` 显示。
- 首版不引入前台 Service；引擎随 APK 进程运行。
- 方向文档最终规划把 `guidecore` 源码放进 APK；首版为复用已验收的 M0 内核，先跨仓构建产物，
  源码迁移另行安排，不阻塞本设计。

## 8. 前端文件与三副本同步

沿用 TR-Stepper 的前端三副本规则：

| 文件 | 角色 |
|---|---|
| `firmware/current/V4.4.1/control_panel.html` | 前端源头 |
| `software/tr-mount-apk/www/index.html` | APK Web 根目录 |
| `android/app/src/main/assets/public/index.html` | Android 构建副本 |

- 修改顺序：先改源头，再 `./sync-html.sh --both` 同步两份；www 的 splash 差异按现有流程处理。
- 新增 `guide_view.js`（导星页轮询、图表、算法 UI），并把它加入 `sync-html.sh` 的 `FILES`。
- `app.js` 继续负责：路由、设备连接、手控、设备参数、赤道仪页与现有 WS 逻辑。
- `control_panel.css` 追加新主题样式；不新建第二套 CSS。
- 旧的 dashboard / goto / calibration / info 结构被新侧栏替换；旧 GOTO、校准逻辑留在 git 历史中，
  后续做对应页面时再移植，首版不保留隐藏入口。

## 9. 错误处理

| 场景 | 行为 |
|---|---|
| 原生桥不存在（浏览器打开） | 顶部横幅「原生导星引擎未加载」；导星操作禁用；赤道仪页仍可走 WS |
| 引擎创建失败 | 横幅显示错误码；Loop/校准/导星禁用 |
| 连续丢星 | 状态 `star_lost`，曲线断点，停止脉冲；5 帧后自动暂停并提示 |
| 校准失败/超时 | 中止当前步，保留上一次有效校准；提示原因；无有效校准时禁止导星 |
| 参数非法 | 字段内联报错，阻止保存，保留旧值 |
| 预览解码失败/超时 | 保留最后一帧，3 s 后标记「画面延迟」 |
| 连接失败（模拟/真实） | 胶囊回到「连接」，状态点变灰，toast 提示 |
| 存储不可用 | 使用内存默认值，控制台告警，不阻塞 UI |
| 应用切后台 | 暂停预览轮询，状态降为 1 Hz；回前台立即刷新；引擎不中断 |

## 10. 验收标准

1. `assembleDebug` 通过；按 TR-Stepper 铁律再跑 `./build.sh compile` 与 `codegraph sync`；
   APK 安装后强制横屏，布局在 800×360 dp 以上无重叠、无内容溢出。
2. 左侧六项 + TRX 字标；点击字标回主页；三个占位页可进入且显示说明。
3. 设备页：模拟连接赤道仪与导星相机成功；主相机/滤镜轮点击给出「未实现」提示。
4. 参数：默认导星像素比例 3.87″/px；焦距改为 400 mm 后变 1.93″/px；重启 APK 后仍保留。
5. 主页：导星面板每秒滚动，四格统计持续更新；单位与导星页联动。
6. 手控器：默认胶囊；展开为 192 px 镂空控件；11 档速度与跟踪开关可用，状态在重启后保留。
7. 赤道仪页：跟踪开启时 RA 角度持续变化；归零/停止/速度控制有可见反馈；地理与校准信息完整显示。
8. 导星页：Loop 出图；校准六步走完；完成校准后「开始导星」可用。
9. 闭环指标：校准后导星 20 s，总 RMS < 0.5 px，且明显低于未导星时的漂移 RMS（> 1.0 px）；
   状态中脉冲毫秒非零，RA/DEC 曲线收敛。
10. 预览与曲线两个面板等大；px/″ 切换同时改变曲线统计、主页统计与校准统计的显示值。
11. 切换算法/拖动参数后，引擎状态中的参数回显同步变化；导星不中断。
12. 页面来回切换，导星会话不中断；返回导星页立即显示最新一帧与曲线。
13. 三份前端文件内容一致；Android assets 为最新；无 JS 异常。

## 11. 测试

- **内核宿主测试**：新增 CTest，在 x86 主机上创建 SimCamera + SimMount，跑完校准 + 60 帧导星，
  断言：校准有效、脉冲非零、RMS 低于阈值、状态 JSON 可解析。接入现有 `ctest`（当前 4/4）。
- **引擎边界**：`get_status` 在小 buffer 下返回所需长度；`get_preview` 无帧返回 0；重复 start/stop 幂等。
- **前端**：无自动化框架；按 §10 手工验收。纯函数（像素比例、速度档映射、px→″）在页面启动时做一次
  自检，失败只写控制台，不弹窗。
- **真机**：只验证模拟闭环与 UI，不接相机/赤道仪；M1 硬件门保持未验证状态。

## 12. 许可与参考

- PHD2 v2.6.14：BSD-3-Clause；APK 分发时保留版权与源码获取方式，新增 `NOTICE`。
- NINA：MPL-2.0；仅参考界面组织与交互，**不复制其代码或资源**。
- TRX 字标为自绘 SVG。
- 视觉稿位于 `TR-Stepper/.superpowers/brainstorm/…/content/`（v5–v9），仅作设计参考，不随 APK 分发。

## 13. 实施前置与风险

1. **版本目录冲突**：TR-Stepper `firmware/current/` 当前同时存在 `V4.4/` 与 `V4.4.1/`，
   违反「只保留一个版本」；`sync-html.sh` 用 `head -1` 会选到 V4.4。
   开工前需由用户确认保留哪个目录（本设计假设 V4.4.1 为当前版），并让同步脚本显式指向它。
2. **闭环阈值依赖模拟参数**：SimMount 漂移与 SimCamera 噪声需固定默认值，否则验收数字不可重复；
   调参只改模拟配置，不调整导星算法。
3. **192 px 手控在窄屏偏小**：以实际设备可点击为准；必要时按屏宽在 192–220 px 间自适应。
4. **跨仓内核版本**：`libguidecore.so` 由 TRXARI 构建后复制；忘记同步会出现 UI 与算法版本不一致。
   用同步脚本 + 状态 JSON 中的 `coreVersion` 字段核对。
5. **M1 未验证**：首版全模拟成功不代表 ZWO USB 可用；相机门仍按方向文档独立验证。

## 14. 视觉参考

- `v7-collapsible.html`：折叠/镂空手控。
- `v8-guide-detail.html`：px/″ 切换、主页动态统计（含可运行演示）。
- `v9-device.html`：设备连接与参数。
- 目录：`D:\Users\LEGLON-TR\Desktop\TR-Stepper\.superpowers\brainstorm\2552-1794376855\content\`
