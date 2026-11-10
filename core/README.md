# GuideCore（M0）

PHD2 v2.6.14 导星算法的最小 C++17 内核，纯算法、无 wx / 无 UI / 无相机 / 无网络。
移植自 BSD-3-Clause 源码，文件头保留出处。

已移植：

| 算法 | 对应 PHD2 文件 |
|---|---|
| Identity / Hysteresis / ResistSwitch | `guide_algorithm_{identity,hysteresis,resistswitch}.cpp` |
| Lowpass / Lowpass2 | `guide_algorithm_{lowpass,lowpass2}.cpp` + `guiding_stats.cpp` |
| ZFilter | `guide_algorithm_zfilter.cpp` + `zfilterfactory.cpp`（Bessel / Butterworth） |
| 图像容器 / 3×3 中值 | `usImage.{h,cpp}` + `image_math.cpp` 的 `Median3` |
| 星点检测 / 质心 / AutoFind | `star.{h,cpp}` + `point.h`（PHD2 全局换成 `AutoFindParams`） |
| 逐帧导星循环 | `guider_multistar.cpp` 的 `UpdateCurrentPosition()`（单星路径）→ `guide_session.{h,cpp}` |
| 校准数学 / 坐标变换 / 脉冲 | `mount.cpp` 的 `SetCalibration`、两个 `Transform*`、`MoveOffset`；`scope.cpp` 的 West/North 轴计算 → `calibration.{h,cpp}` |
| 校准状态机 | `scope.cpp` 的 `BeginCalibration` / `UpdateCalibrationState`（West→East→回差→North→South→Nudge）→ `CalibrationSession` |

**注意**：PHD2 的 `SetMinMove` 等 setter 会写配置；这里去掉了持久化，
只保留纯计算。`defaults(type)` 返回 PHD2 的默认参数。

## 构建与测试

```bash
cmake -S core -B core/build -G "MinGW Makefiles"
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

Linux/NDK 下把 generator 换掉即可。测试覆盖：算法手算期望值、完美赤道仪闭环
（40 项）、星点检测（20 项）、帧级导星循环（236 项）、校准数学与状态机
（10273 项，含 PHD2 `TestTransforms()` 的全部角度组合和一次完整四方向校准）。

## 可视化验收

```bash
python core/tools/make_report.py
```

`guide_sim` 生成 600 帧合成导星曲线（周期误差 + 慢漂 + 视宁度噪声），
让 6 种算法分别跑闭环（理想赤道仪、1 帧响应延迟），脚本把结果写成
`core/build/guide-report.html`：RA/DEC 原始漂移与残差曲线、修正量、
RMS 对比条图和汇总表。全部数字来自 C++ 内核，浏览器只负责绘图。

> 当前范围仅算法内核闭环，不含相机/赤道仪。

帧级回放（星图 → 选星 → 逐帧重找 → 偏移 → 算法修正 → 赤道仪）：

```bash
python core/tools/make_replay_report.py
```

`guide_replay` 先用 `CalibrationSession` 在合成赤道仪上跑完整校准，得到
xAngle/yAngle/xRate/yRate；之后每帧渲染 121×121 星图（背景 + 噪声 + 高斯 PSF），
`AutoFind` 锁星 → 逐帧 `Star::Find(searchRegion, 上次位置)` → 相机→赤道仪变换 →
导星算法 → 有符号脉冲毫秒（1 ms 量化）→ 合成赤道仪（1 帧延迟）。页面显示残差、
累计漂移 vs 实际修正、脉冲毫秒、SNR/HFD，以及 6 种算法的 RMS/脉冲汇总。

交互式导星模拟（浏览器控制，不是静态报告）：

```bash
python core/tools/guide_live_server.py        # 打开 http://127.0.0.1:8765
```

后台常驻 `guide_live` 进程，一行命令进、一行 JSON 出（`step` / `algo:N` /
`minmove:X` / `seeing:X` / `drift:X` / `dither` / `calibrate` / `reset`）。
页面可以开始/暂停、切换 6 种算法、调 minMove / 视宁度 / 漂移速率、给星点加
3 px 扰动、重新校准和重置；左侧是 121×121 实时星场（绿框为 15 px 搜索区），
右侧是残差与脉冲曲线，指标为滚动 200 帧的 RMS/峰值。内核仍是同一套
`GuideSession` + `CalibrationSession`，页面只发命令和画图。

校准状态机验收页：

```bash
python core/tools/make_cal_report.py
```

`cal_sim` 用合成赤道仪驱动 `CalibrationSession`：星点按脉冲方向/毫秒移动，
页面显示完整路径（West 测量 → East 回中 → 回差清除 → North 测量 → South 回中 →
微调回起点）、每步方向/时长，以及算出的 xAngle/yAngle/xRate/yRate 与正交误差。

星点检测另有验收页：

```bash
python core/tools/make_star_report.py
```

`star_sim` 生成 640×480 合成星场（高斯 PSF + 噪声 + 热像素），调用移植后的
`GuideStar::AutoFind`，输出全画幅检出图、主星放大图（质心/HFD/搜索框）和误差表。
参数取 PHD2 默认：`searchRegion=15`、`minHFD=1.5`、`maxHFD=20`、`minSNR=6`。
`AutoFind` 原本依赖的 `pFrame/pCamera/pGuider` 已收敛为 `AutoFindParams`，算法流程未改。

## 尚未移植

- 校准的 RA/Dec parity（需要赤道仪 RA/Dec 回读）、进度/告警通知
- 多星加权偏差（`RefineOffset`）与星等突变检查（`MassChecker`）
- 导星脉冲/反转统计（算法用不到）
- 高斯过程算法（可选，最后）
- `guide_algorithm.cpp` 里的 SmartDefaultMinMove（依赖焦距/像元尺度，放参数层）
