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

**注意**：PHD2 的 `SetMinMove` 等 setter 会写配置；这里去掉了持久化，
只保留纯计算。`defaults(type)` 返回 PHD2 的默认参数。

## 构建与测试

```bash
cmake -S core -B core/build -G "MinGW Makefiles"
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

Linux/NDK 下把 generator 换掉即可。测试覆盖：算法手算期望值、完美赤道仪闭环
（40 项）、星点检测（20 项）、帧级导星循环（236 项）、校准数学（10242 项，
含 PHD2 `TestTransforms()` 的全部角度组合）。

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

`guide_replay` 每帧渲染 121×121 星图（背景 + 噪声 + 高斯 PSF），先用
`AutoFind` 锁星，再逐帧调用与 PHD2 相同的 `Star::Find(searchRegion, 上次位置)`，
把 `star - lockPos` 交给导星算法；赤道仪按 1 帧延迟应用修正。页面显示残差曲线、
累计漂移 vs 累计修正、修正量、SNR/HFD，以及 6 种算法的 RMS 汇总。

星点检测另有验收页：

```bash
python core/tools/make_star_report.py
```

`star_sim` 生成 640×480 合成星场（高斯 PSF + 噪声 + 热像素），调用移植后的
`GuideStar::AutoFind`，输出全画幅检出图、主星放大图（质心/HFD/搜索框）和误差表。
参数取 PHD2 默认：`searchRegion=15`、`minHFD=1.5`、`maxHFD=20`、`minSNR=6`。
`AutoFind` 原本依赖的 `pFrame/pCamera/pGuider` 已收敛为 `AutoFindParams`，算法流程未改。

## 尚未移植

- 交互式校准状态机（`Scope::UpdateCalibrationState` 的 East/South 回中、
  回差清除、nudge、进度通知）；轴求解与坐标变换已完成
- 多星加权偏差（`RefineOffset`）与星等突变检查（`MassChecker`）
- 导星脉冲/反转统计（算法用不到）
- 高斯过程算法（可选，最后）
- `guide_algorithm.cpp` 里的 SmartDefaultMinMove（依赖焦距/像元尺度，放参数层）
