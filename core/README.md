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

**注意**：PHD2 的 `SetMinMove` 等 setter 会写配置；这里去掉了持久化，
只保留纯计算。`defaults(type)` 返回 PHD2 的默认参数。

## 构建与测试

```bash
cmake -S core -B core/build -G "MinGW Makefiles"
cmake --build core/build
./core/build/test_algorithms.exe
```

Linux/NDK 下把 generator 换掉即可。测试覆盖手算期望值，以及
“完美响应赤道仪”闭环下残差收敛到 0。

## 可视化验收

```bash
python core/tools/make_report.py
```

`guide_sim` 生成 600 帧合成导星曲线（周期误差 + 慢漂 + 视宁度噪声），
让 6 种算法分别跑闭环（理想赤道仪、1 帧响应延迟），脚本把结果写成
`core/build/guide-report.html`：RA/DEC 原始漂移与残差曲线、修正量、
RMS 对比条图和汇总表。全部数字来自 C++ 内核，浏览器只负责绘图。

> 当前范围仅算法内核闭环，不含相机/赤道仪。

星点检测另有验收页：

```bash
python core/tools/make_star_report.py
```

`star_sim` 生成 640×480 合成星场（高斯 PSF + 噪声 + 热像素），调用移植后的
`GuideStar::AutoFind`，输出全画幅检出图、主星放大图（质心/HFD/搜索框）和误差表。
参数取 PHD2 默认：`searchRegion=15`、`minHFD=1.5`、`maxHFD=20`、`minSNR=6`。
`AutoFind` 原本依赖的 `pFrame/pCamera/pGuider` 已收敛为 `AutoFindParams`，算法流程未改。

## 尚未移植

- `StarDisplacement` 的导星脉冲/反转统计（算法用不到）
- 高斯过程算法（可选，最后）
- `guide_algorithm.cpp` 里的 SmartDefaultMinMove（依赖焦距/像元尺度，放参数层）
