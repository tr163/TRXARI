# 导星 APK 原生层实现计划（Plan A）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 TRXARI `guidecore` 增加可被 APK 调用的模拟导星引擎（SimCamera + SimMount + `gc_engine_*` C API），并把它通过 JNI + Capacitor 插件接进 TR-Stepper APK；本计划结束时 arm64 APK 能编译、打包两个 `.so`，宿主闭环测试通过。

**Architecture:** 引擎复用现有 `GuideSession` / `CalibrationSession`，在内部线程按曝光周期合成星场、测量质心、跑校准/导星算法并把修正施加到 SimMount。JS 只通过 5 Hz 状态 JSON 和 1 fps 预览帧消费；JNI 不做回调，只做同步调用。首版不接真实相机/赤道仪。

**Tech Stack:** C++17、CMake 4.4 + Ninja、Android NDK r27c、Capacitor 7、Java 17+（项目现有 MainActivity 是 Java）、Gradle 8.11。

**前置约定：**
- 不修改 `firmware/current/` 下的固件逻辑；前端源头假定为 `firmware/current/V4.4.1/`。
- TR-Stepper 动手前先建备份分支：`git branch backup-guide-ui-20261111`。
- 不连真机跑功能测试；本计划的验收只到编译、宿主测试、APK 打包检查。
- 所有路径以本机为准：TRXARI=`D:/Users/LEGLON-TR/Desktop/TRXARI`，TR-Stepper=`D:/Users/LEGLON-TR/Desktop/TR-Stepper`，NDK=`D:/tools/android/android-ndk-r27c`。

---

## Task 1: 引擎 C 接口与模拟驱动

**Files:**
- Create: `TRXARI/core/include/guidecore/engine.h`
- Create: `TRXARI/core/include/guidecore/sim_drivers.h`
- Create: `TRXARI/core/src/sim_drivers.cpp`
- Modify: `TRXARI/core/CMakeLists.txt`

- [ ] **Step 1: 写 `engine.h`**

```c
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GcEngineState { GC_IDLE = 0, GC_LOOPING = 1, GC_CALIBRATING = 2, GC_GUIDING = 3, GC_STAR_LOST = 4 };
enum GcCalibrationStep {
    GC_CAL_IDLE = 0, GC_CAL_WEST = 1, GC_CAL_EAST = 2, GC_CAL_BACKLASH = 3,
    GC_CAL_NORTH = 4, GC_CAL_SOUTH = 5, GC_CAL_RECENTER = 6, GC_CAL_DONE = 7, GC_CAL_FAILED = 8
};

typedef struct GcEngineStats {
    int32_t state;
    int32_t frame;
    int32_t starValid;
    int32_t calibrationValid;
    int32_t calibrationStep;
    int32_t samples;
    int32_t lastRaMs;
    int32_t lastDecMs;
    int32_t meanPulseMs;
    double snr;
    double hfd;
    double dx;
    double dy;
    double rmsRa;
    double rmsDec;
    double rmsTotal;
    double peak;
    double raDeg;
    double decDeg;
    int32_t tracking;
    int32_t slewRate;
} GcEngineStats;

int32_t gc_engine_create(const char *configJson);   // 0 ok, <0 error
void    gc_engine_destroy(void);
int32_t gc_engine_step(void);                       // 前进一帧；宿主测试用

int32_t gc_engine_start_loop(void);
int32_t gc_engine_stop_loop(void);
int32_t gc_engine_start_calibration(void);
int32_t gc_engine_start_guide(void);                // 未校准时返回 -3
int32_t gc_engine_stop(void);                       // 停止导星/校准，回到 loop
int32_t gc_engine_clear_calibration(void);

int32_t gc_engine_set_params(const char *json);
int32_t gc_engine_set_tracking(int32_t on);
int32_t gc_engine_set_slew_rate(int32_t rate);      // 2..2048
int32_t gc_engine_manual_move(int32_t axis, int32_t dir, int32_t on); // axis 0=RA 1=DEC
int32_t gc_engine_home(void);

int32_t gc_engine_get_stats(GcEngineStats *out);
int32_t gc_engine_preview_size(int32_t *w, int32_t *h);
int32_t gc_engine_get_preview(uint8_t *out, int32_t cap); // 8-bit gray, w*h；无帧返回 0
int32_t gc_engine_get_status(char *out, int32_t cap);     // 返回含 '\0' 的所需长度

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写 `sim_drivers.h`**

```cpp
#pragma once
#include "guidecore/calibration.h"
#include "guidecore/us_image.h"

#include <cstdint>
#include <vector>

namespace gc {

struct SimMountConfig {
    int width = 640;
    int height = 480;
    double raDriftPxPerSec = 0.8;
    double decDriftPxPerSec = 0.3;
    double untrackedRaDriftPxPerSec = 2.0;
    double slewPxPerSecAt512 = 2.0;
    uint32_t seed = 424242;
};

class SimMount {
public:
    explicit SimMount(const SimMountConfig &cfg = SimMountConfig());
    void reset();
    void advance(double dtSec);
    void pulse(CalibrationDirection dir, int durationMs);
    void applyGuideCorrection(double cameraDx, double cameraDy);
    void setTracking(bool on) { m_tracking = on; }
    bool tracking() const { return m_tracking; }
    void setSlewRate(int rate);
    int slewRate() const { return m_slewRate; }
    void manualMove(int axis, int dir, bool on);
    void setDriftEnabled(bool on) { m_driftEnabled = on; }
    double starX() const { return m_x; }
    double starY() const { return m_y; }
    double raDeg() const { return 123.0 + m_x * 0.01; }
    double decDeg() const { return 45.0 + m_y * 0.01; }
private:
    SimMountConfig m_cfg;
    double m_x = 0.0, m_y = 0.0;
    bool m_tracking = false;
    bool m_driftEnabled = true;
    int m_slewRate = 512;
    int m_manualAxis = -1;
    int m_manualDir = 0;
};

class SimCamera {
public:
    explicit SimCamera(const SimMountConfig &cfg = SimMountConfig());
    void render(const SimMount &mount, usImage &img, uint32_t frame,
                double jitterX, double jitterY, bool lowNoise) const;
    void toPreview(const usImage &img, std::vector<uint8_t> &out) const;
    int width() const { return m_cfg.width; }
    int height() const { return m_cfg.height; }
private:
    SimMountConfig m_cfg;
};

} // namespace gc
```

- [ ] **Step 3: 实现 `sim_drivers.cpp`**

关键点（完整实现按此写）：
- `pulse()` 与 `guide_live.cpp` 一致：West/East 沿 `kWestAngle=0.4`，North/South 沿 `kNorthAngle=0.4+π/2-0.05`，速度 `0.02 px/ms`；West/North 取正，East/South 取负。
- `advance()`：`ra = tracking ? raDrift : untrackedRaDrift`，`m_x += ra*dt`，`m_y += decDrift*dt`；`m_driftEnabled=false` 时跳过漂移；手动移动按 `m_slewRate/512 * slewPxPerSecAt512 * dt` 累加，axis 0 加 x，axis 1 加 y。
- `applyGuideCorrection(dx,dy)`：`m_x -= dx; m_y -= dy;`。
- `setSlewRate()`：只接受 2 的整数次幂 2..2048，非法值保持原值。
- `SimCamera::render()`：640×480，背景 800 ADU + 高斯噪声（σ=6，校准帧 σ=2），主星为 σ=1.8、幅度 12000 的高斯；星心 `center + mount.starX() + jitterX`。为控制 CPU，背景全帧填噪声，星点只在 48×48 邻域叠加。
- `toPreview()`：`v = clamp((pixel - 700) / 3300 * 255, 0, 255)`。

- [ ] **Step 4: CMake 加入源文件**

`core/CMakeLists.txt` 的 `add_library(guidecore ...)` 追加：

```cmake
    src/sim_drivers.cpp
    src/engine.cpp
```

- [ ] **Step 5: 编译（此时 engine.cpp 还不存在会失败，先只验证 sim_drivers）**

先临时只加 `src/sim_drivers.cpp`，跑：

```bash
cmake -S core -B core/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build core/build
```

Expected: `libguidecore.a` 编过。

- [ ] **Step 6: 提交**

```bash
git add core/include/guidecore/sim_drivers.h core/src/sim_drivers.cpp core/CMakeLists.txt
git commit -m "core: 增加导星模拟赤道仪与星场渲染"
```

---

## Task 2: 引擎实现（线程、校准、导星、JSON）

**Files:**
- Create: `TRXARI/core/src/engine.cpp`

- [ ] **Step 1: 实现全局单例与线程**

```cpp
namespace {
std::mutex g_mutex;
std::unique_ptr<Engine> g_engine;

struct EngineConfig {
    int exposureMs = 1000;
    uint32_t seed = 424242;
    double jitterPx = 0.08;
};

class Engine { ... };
}
```

- `gc_engine_create`：若已有实例先 destroy；解析 `exposureMs`、`seed`、`jitterPx`、`raDrift`、`decDrift`；创建 `SimMount`/`SimCamera`/`GuideSession`。
- 工作线程：`startWorker()` 起 `std::thread`，按 `exposureMs` 调 `tick()`；`stopWorker()` 用 `condition_variable` 唤醒并 join。`gc_engine_step()` 直接调 `tick()`。
- `tick()` 顺序：`sim.advance(dt)` → `camera.render(...)` → 按状态测量/校准/导星 → 更新预览与快照。

- [ ] **Step 2: 状态机**

| 状态 | 每帧行为 |
|---|---|
| `Looping` | `session.acquire(img)`；成功则 `starValid=true` |
| `Calibrating` | 首帧 acquire 后 `calSession.begin(starPos, 0.0, 0)`；此后 `session.processFrame()` 得绝对星位，喂 `calSession.update()`；成功则提交校准并回 `Looping` |
| `Guiding` | `session.processFrame()`；成功则把 `step.raCorrectionPx/decCorrectionPx` 经 `mountToCamera` 转成相机向量后 `sim.applyGuideCorrection()`，并记录 `step.mountRaPx/mountDecPx` 到统计窗口 |
| `StarLost` | 每帧尝试 `session.acquire()`；成功回 `Guiding` |

- 校准参数固定为 `stepMs=250, distancePx=10, maxMovePixels=15`；校准期间 `sim.setDriftEnabled(false)`，跟踪强制 ON，相机 `lowNoise=true`；完成后恢复漂移、以新校准重建 `GuideSession`。
- 导星连续丢星 5 帧 → `StarLost`；`StarLost` 再丢 5 帧 → `Looping`。
- `start_guide()` 在 `calibrationValid=false` 时返回 `-3`；`clear_calibration()` 清校准并把导星退回 `Looping`。
- 统计窗口 300 点；RMS 用 `sqrt(mean(v²))`；`peak` 取 RA/DEC 绝对值最大；`meanPulseMs` 取 `hypot(raMs,decMs)` 均值。

- [ ] **Step 3: 参数 JSON**

`gc_engine_set_params` 解析这些键：`raAlgo`、`decAlgo`、`minMove`、`aggression`、`hysteresis`、`slopeWeight`、`aggressiveness`、`expFactor`、`fastSwitch`、`guideRate`。
- 算法名：`identity / hysteresis / resistswitch / lowpass / lowpass2 / zfilter`（大小写不敏感）。
- `aggression` 与 `hysteresis` 传百分比（70、10），内部除以 100；`minMove` 单位 px。
- 失败返回 `-4`，不改旧参数。
- 参数变更后重建 `GuideSession`（保留校准），下一帧自动重新锁星；状态不退出 `Guiding`。

- [ ] **Step 4: 状态 JSON**

按设计文档 §7.2 输出，额外加 `"previewW":640,"previewH":480,"engine":{"coreVersion":"a1"}`。字符串固定，用 `snprintf` 构建；`gc_engine_get_status` 返回所需长度，buffer 不足时不写入。

- [ ] **Step 5: 手动移动/归零/跟踪/速度**

- `manual_move(axis,dir,on)` 直接转给 SimMount；`axis` 只接受 0/1，`dir` 只接受 -1/1。
- `home()` 重置 SimMount、清统计、导星退回 `Looping`（若正在导星）。
- `set_tracking(on)`、`set_slew_rate(rate)` 更新 SimMount 并同步状态。

- [ ] **Step 6: 编译并修到通过**

```bash
cmake -S core -B core/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build core/build
```

Expected: `libguidecore.a` 编过，无警告优先。

- [ ] **Step 7: 提交**

```bash
git add core/src/engine.cpp core/CMakeLists.txt
git commit -m "core: 增加可线程运行的模拟导星引擎与 gc_engine 接口"
```

---

## Task 3: 宿主闭环测试

**Files:**
- Create: `TRXARI/core/tests/test_engine.cpp`
- Modify: `TRXARI/core/CMakeLists.txt`

- [ ] **Step 1: 写测试**

```cpp
#include "guidecore/engine.h"
#include <cmath>
#include <cstdio>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    CHECK(gc_engine_create("{\"exposureMs\":1000,\"seed\":7,\"jitterPx\":0.0}") == 0);
    CHECK(gc_engine_set_tracking(1) == 0);
    CHECK(gc_engine_start_calibration() == 0);

    GcEngineStats s{};
    for (int i = 0; i < 400; ++i) {
        CHECK(gc_engine_step() == 0);
        CHECK(gc_engine_get_stats(&s) == 0);
        if (s.state != GC_CALIBRATING) break;
    }
    CHECK(s.calibrationValid == 1);

    CHECK(gc_engine_start_guide() == 0);
    for (int i = 0; i < 60; ++i) {
        CHECK(gc_engine_step() == 0);
    }
    CHECK(gc_engine_get_stats(&s) == 0);
    CHECK(s.starValid == 1);
    CHECK(s.rmsTotal < 0.5);
    CHECK(s.meanPulseMs > 0);

    int32_t w = 0, h = 0;
    CHECK(gc_engine_preview_size(&w, &h) == 0);
    CHECK(w == 640 && h == 480);
    CHECK(gc_engine_get_preview(nullptr, 0) == w * h);

    char small[8];
    const int need = gc_engine_get_status(small, sizeof small);
    CHECK(need > (int) sizeof small);

    gc_engine_destroy();
    std::printf("engine: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
```

- [ ] **Step 2: 加 CMake 目标**

```cmake
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE guidecore)
add_test(NAME engine COMMAND test_engine)
```

- [ ] **Step 3: 跑测试**

```bash
cmake -S core -B core/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

Expected: `5/5 tests passed`（原 4 项 + engine）。

- [ ] **Step 4: 如果 RMS 不达标**

只调模拟参数，不动导星算法：优先把 `CalibrationParams.stepMs` 改为 250、`distancePx` 改为 10；仍不收敛再降低 `raDriftPxPerSec` 到 0.6。把最终值写进 `engine.cpp` 默认配置。

- [ ] **Step 5: 提交**

```bash
git add core/tests/test_engine.cpp core/CMakeLists.txt
git commit -m "test: 增加模拟校准+导星闭环测试"
```

---

## Task 4: arm64 内核构建

- [ ] **Step 1: 构建**

```bash
cmake -S core -B core/build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=D:/tools/android/android-ndk-r27c/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build core/build/android
```

Expected: `core/build/android/libguidecore.so`。

- [ ] **Step 2: 验证**

```bash
file core/build/android/libguidecore.so
```

Expected: `ELF 64-bit LSB shared object, ARM aarch64`。

- [ ] **Step 3: 提交（若产物目录被跟踪则跳过）**

构建产物在 `core/build/`，通常被 .gitignore 忽略；本步只记录，不提交 `.so`。

---

## Task 5: JNI 薄层

**Files:**
- Create: `TR-Stepper/software/tr-mount-apk/android/app/src/main/cpp/CMakeLists.txt`
- Create: `TR-Stepper/software/tr-mount-apk/android/app/src/main/cpp/guide_jni.cpp`
- Create: `TR-Stepper/software/tr-mount-apk/android/app/src/main/java/com/tr/equatorial/guide/NativeGuideBridge.java`

- [ ] **Step 1: 写 `NativeGuideBridge.java`**

```java
package com.tr.equatorial.guide;

final class NativeGuideBridge {
    static { System.loadLibrary("guide_bridge"); }

    static native int create(String configJson);
    static native void destroy();
    static native int step();
    static native int startLoop();
    static native int stopLoop();
    static native int startCalibration();
    static native int startGuide();
    static native int stop();
    static native int clearCalibration();
    static native int setParams(String json);
    static native int setTracking(int on);
    static native int setSlewRate(int rate);
    static native int manualMove(int axis, int dir, int on);
    static native int home();
    static native String getStatus();
    static native byte[] getPreview();
    static native int[] previewSize();
}
```

- [ ] **Step 2: 写 `guide_jni.cpp`**

```cpp
#include <jni.h>
#include "guidecore/engine.h"

#include <string>
#include <vector>

namespace {
std::string ToString(JNIEnv *env, jstring s)
{
    if (!s) return {};
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string out(c ? c : "");
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_tr_equatorial_guide_NativeGuideBridge_create(JNIEnv *env, jclass, jstring cfg)
{ return gc_engine_create(ToString(env, cfg).c_str()); }

extern "C" JNIEXPORT void JNICALL
Java_com_tr_equatorial_guide_NativeGuideBridge_destroy(JNIEnv *, jclass) { gc_engine_destroy(); }

// step/startLoop/.../home 都是同构的一行转发，照下面 create 的写法补全。
extern "C" JNIEXPORT jint JNICALL
Java_com_tr_equatorial_guide_NativeGuideBridge_step(JNIEnv *, jclass) { return gc_engine_step(); }

extern "C" JNIEXPORT jstring JNICALL
Java_com_tr_equatorial_guide_NativeGuideBridge_getStatus(JNIEnv *env, jclass)
{
    const int need = gc_engine_get_status(nullptr, 0);
    if (need <= 0) return env->NewStringUTF("{}");
    std::vector<char> buf((size_t) need);
    gc_engine_get_status(buf.data(), need);
    return env->NewStringUTF(buf.data());
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_tr_equatorial_guide_NativeGuideBridge_previewSize(JNIEnv *env, jclass)
{
    int32_t w = 0, h = 0;
    gc_engine_preview_size(&w, &h);
    jintArray arr = env->NewIntArray(2);
    const jint v[2] = { w, h };
    env->SetIntArrayRegion(arr, 0, 2, v);
    return arr;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_tr_equatorial_guide_NativeGuideBridge_getPreview(JNIEnv *env, jclass)
{
    const int need = gc_engine_get_preview(nullptr, 0);
    if (need <= 0) return nullptr;
    std::vector<uint8_t> buf((size_t) need);
    if (gc_engine_get_preview(buf.data(), need) != need) return nullptr;
    jbyteArray arr = env->NewByteArray(need);
    env->SetByteArrayRegion(arr, 0, need, reinterpret_cast<const jbyte *>(buf.data()));
    return arr;
}
```

- [ ] **Step 3: 写 CMake**

```cmake
cmake_minimum_required(VERSION 3.18)
project(guide_bridge CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(guide_bridge SHARED guide_jni.cpp)
target_include_directories(guide_bridge PRIVATE "${GUIDECORE_INCLUDE_DIR}")
target_link_libraries(guide_bridge PRIVATE "${GUIDECORE_LIBRARY}" log)
```

- [ ] **Step 4: 构建**

```bash
cmake -S software/tr-mount-apk/android/app/src/main/cpp \
  -B software/tr-mount-apk/build/native/bridge -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=D:/tools/android/android-ndk-r27c/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release \
  -DGUIDECORE_INCLUDE_DIR=D:/Users/LEGLON-TR/Desktop/TRXARI/core/include \
  -DGUIDECORE_LIBRARY=D:/Users/LEGLON-TR/Desktop/TRXARI/core/build/android/libguidecore.so
cmake --build software/tr-mount-apk/build/native/bridge
```

Expected: `libguide_bridge.so`，`readelf -d` 含 `NEEDED libguidecore.so`。

---

## Task 6: Capacitor 插件与 APK 配置

**Files:**
- Create: `TR-Stepper/software/tr-mount-apk/android/app/src/main/java/com/tr/equatorial/guide/PreviewCodec.java`
- Create: `TR-Stepper/software/tr-mount-apk/android/app/src/main/java/com/tr/equatorial/guide/GuideBridgePlugin.java`
- Modify: `TR-Stepper/software/tr-mount-apk/android/app/src/main/java/com/tr/equatorial/MainActivity.java`
- Modify: `TR-Stepper/software/tr-mount-apk/android/app/src/main/AndroidManifest.xml`

- [ ] **Step 1: 写 `PreviewCodec.java`**

```java
package com.tr.equatorial.guide;

import android.graphics.Bitmap;
import android.util.Base64;
import java.io.ByteArrayOutputStream;

final class PreviewCodec {
    private PreviewCodec() {}

    static String grayToJpegBase64(byte[] gray, int w, int h, int quality) {
        int[] argb = new int[w * h];
        for (int i = 0; i < argb.length; i++) {
            int g = gray[i] & 0xff;
            argb[i] = 0xff000000 | (g << 16) | (g << 8) | g;
        }
        Bitmap bmp = Bitmap.createBitmap(argb, w, h, Bitmap.Config.ARGB_8888);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        bmp.compress(Bitmap.CompressFormat.JPEG, quality, out);
        bmp.recycle();
        return Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP);
    }
}
```

- [ ] **Step 2: 写 `GuideBridgePlugin.java`**

```java
package com.tr.equatorial.guide;

import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

@CapacitorPlugin(name = "GuideBridge")
public class GuideBridgePlugin extends Plugin {
    @Override
    public void load() {
        NativeGuideBridge.create("{\"exposureMs\":1000}");
    }

    @Override
    protected void handleOnDestroy() {
        NativeGuideBridge.destroy();
        super.handleOnDestroy();
    }

    private void code(PluginCall call, int rc) {
        JSObject ret = new JSObject();
        ret.put("code", rc);
        call.resolve(ret);
    }

    @PluginMethod public void startLoop(PluginCall call) { code(call, NativeGuideBridge.startLoop()); }
    @PluginMethod public void stopLoop(PluginCall call) { code(call, NativeGuideBridge.stopLoop()); }
    @PluginMethod public void startCalibration(PluginCall call) { code(call, NativeGuideBridge.startCalibration()); }
    @PluginMethod public void startGuiding(PluginCall call) { code(call, NativeGuideBridge.startGuide()); }
    @PluginMethod public void stop(PluginCall call) { code(call, NativeGuideBridge.stop()); }
    @PluginMethod public void clearCalibration(PluginCall call) { code(call, NativeGuideBridge.clearCalibration()); }
    @PluginMethod public void setTracking(PluginCall call) { code(call, NativeGuideBridge.setTracking(call.getBoolean("on", false) ? 1 : 0)); }
    @PluginMethod public void setSlewRate(PluginCall call) { code(call, NativeGuideBridge.setSlewRate(call.getInt("rate", 512))); }
    @PluginMethod public void manualMove(PluginCall call) {
        code(call, NativeGuideBridge.manualMove(call.getInt("axis", 0), call.getInt("dir", 1),
                call.getBoolean("on", false) ? 1 : 0));
    }
    @PluginMethod public void home(PluginCall call) { code(call, NativeGuideBridge.home()); }
    @PluginMethod public void setParams(PluginCall call) { code(call, NativeGuideBridge.setParams(call.getString("json", "{}"))); }

    @PluginMethod
    public void getStatus(PluginCall call) {
        JSObject ret = new JSObject();
        ret.put("status", NativeGuideBridge.getStatus());
        call.resolve(ret);
    }

    @PluginMethod
    public void getPreview(PluginCall call) {
        byte[] gray = NativeGuideBridge.getPreview();
        if (gray == null) { call.resolve(); return; }
        int[] wh = NativeGuideBridge.previewSize();
        JSObject ret = new JSObject();
        ret.put("jpeg", PreviewCodec.grayToJpegBase64(gray, wh[0], wh[1], 70));
        call.resolve(ret);
    }
}
```

- [ ] **Step 3: 注册插件、强制横屏**

`MainActivity.java` 在 `super.onCreate(savedInstanceState);` 之前加：

```java
import com.tr.equatorial.guide.GuideBridgePlugin;
...
registerPlugin(GuideBridgePlugin.class);
```

`AndroidManifest.xml` 的 `<activity>` 加：

```xml
android:screenOrientation="sensorLandscape"
```

- [ ] **Step 4: 编译 Java（Gradle）**

```bash
cd software/tr-mount-apk/android
JAVA_HOME=C:/tools/jdk-21.0.12.1+1 ./gradlew assembleDebug
```

Expected: `BUILD SUCCESSFUL`；若插件类找不到，检查 `GuideBridgePlugin.java` 包名与 `registerPlugin` 导入。

---

## Task 7: 一键构建脚本与 APK 打包检查

**Files:**
- Create: `TR-Stepper/software/tr-mount-apk/scripts/build-native.sh`

- [ ] **Step 1: 写脚本**

```bash
#!/usr/bin/env bash
set -euo pipefail

NDK="${ANDROID_NDK_HOME:-D:/tools/android/android-ndk-r27c}"
TRXARI="${TRXARI_ROOT:-D:/Users/LEGLON-TR/Desktop/TRXARI}"
APK="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$APK/build/native"
JNI_LIBS="$APK/android/app/src/main/jniLibs/arm64-v8a"

cmake -S "$TRXARI/core" -B "$BUILD/core" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD/core"

cmake -S "$APK/android/app/src/main/cpp" -B "$BUILD/bridge" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release \
  -DGUIDECORE_INCLUDE_DIR="$TRXARI/core/include" \
  -DGUIDECORE_LIBRARY="$BUILD/core/libguidecore.so"
cmake --build "$BUILD/bridge"

mkdir -p "$JNI_LIBS"
cp "$BUILD/core/libguidecore.so" "$BUILD/bridge/libguide_bridge.so" "$JNI_LIBS/"
ls -l "$JNI_LIBS"
```

- [ ] **Step 2: 跑脚本**

```bash
chmod +x software/tr-mount-apk/scripts/build-native.sh
software/tr-mount-apk/scripts/build-native.sh
```

Expected: 两个 `.so` 出现在 `jniLibs/arm64-v8a/`。

- [ ] **Step 3: 打 APK 并确认打包**

```bash
cd software/tr-mount-apk/android
JAVA_HOME=C:/tools/jdk-21.0.12.1+1 ./gradlew assembleDebug
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep 'lib/arm64-v8a/libguide'
```

Expected: 输出 `libguidecore.so` 与 `libguide_bridge.so`。

- [ ] **Step 4: 提交**

```bash
git add software/tr-mount-apk/android/app/src/main/cpp \
        software/tr-mount-apk/android/app/src/main/java/com/tr/equatorial/guide \
        software/tr-mount-apk/android/app/src/main/java/com/tr/equatorial/MainActivity.java \
        software/tr-mount-apk/android/app/src/main/AndroidManifest.xml \
        software/tr-mount-apk/scripts/build-native.sh
git commit -m "APK: 接入 guidecore 原生引擎与 Capacitor 导星桥"
```

---

## Task 8: 收尾检查

- [ ] 宿主 `ctest` 全绿（5/5）。
- [ ] arm64 `.so` 为 `ELF 64-bit AArch64`。
- [ ] APK 内含两个 `.so`。
- [ ] 未连接真机、未发 `:4030`/`:81` 功能命令。
- [ ] `git status` 无 `.claude/settings.local.json`、无 `local.properties` 以外的本地文件。
- [ ] 把结果写进 `README.md` 状态表与 `docs/DIRECTION-guiding-app.md`，提交并推送。

---

## 验收边界

本计划只交付“原生引擎 + JNI + APK 可打包”。界面交互、曲线、手控器与相机预览属于 Plan B；本计划结束时用户应能看到 APK 安装包，但导星页尚未接 UI。真机功能测试仍由用户执行。
