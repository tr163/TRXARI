// Acceptance harness for the ported calibration state machine.
// Drives CalibrationSession against a synthetic mount and prints the star path
// as JSON for tools/make_cal_report.py.
#include "guidecore/calibration.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;

struct SimMount
{
    double ratePxPerMs = 0.02;
    double westAngle = 0.4;
    double northAngle = 0.4 + PI / 2.0 - 0.05;
    gc::PHD_Point pos{ 100.0, 80.0 };

    void pulse(gc::CalibrationDirection dir, int ms)
    {
        double angle = 0.0, sign = 1.0;
        switch (dir)
        {
        case gc::CalibrationDirection::West: angle = westAngle; sign = 1.0; break;
        case gc::CalibrationDirection::East: angle = westAngle; sign = -1.0; break;
        case gc::CalibrationDirection::North: angle = northAngle; sign = 1.0; break;
        case gc::CalibrationDirection::South: angle = northAngle; sign = -1.0; break;
        case gc::CalibrationDirection::None: return;
        }
        pos.SetXY(pos.X + std::cos(angle) * ratePxPerMs * ms * sign,
                  pos.Y + std::sin(angle) * ratePxPerMs * ms * sign);
    }
};

const char* dirName(gc::CalibrationDirection d)
{
    switch (d)
    {
    case gc::CalibrationDirection::West: return "West";
    case gc::CalibrationDirection::East: return "East";
    case gc::CalibrationDirection::North: return "North";
    case gc::CalibrationDirection::South: return "South";
    default: return "Start";
    }
}

const char* stateName(gc::CalibrationState s)
{
    switch (s)
    {
    case gc::CalibrationState::GoWest: return "GoWest";
    case gc::CalibrationState::GoEast: return "GoEast";
    case gc::CalibrationState::ClearBacklash: return "ClearBacklash";
    case gc::CalibrationState::GoNorth: return "GoNorth";
    case gc::CalibrationState::GoSouth: return "GoSouth";
    case gc::CalibrationState::NudgeSouth: return "NudgeSouth";
    case gc::CalibrationState::Complete: return "Complete";
    default: return "Cleared";
    }
}

double normAngle(double v) { return v - std::floor((v + PI) / (2.0 * PI)) * (2.0 * PI); }

} // namespace

int main()
{
    SimMount mount;
    const gc::PHD_Point start = mount.pos;

    gc::CalibrationSession session;
    session.begin(start);

    struct Step
    {
        double x, y;
        gc::CalibrationDirection dir;
        gc::CalibrationState state;
        int ms;
        double dist;
    };
    std::vector<Step> path;
    path.push_back({ start.X, start.Y, gc::CalibrationDirection::None, gc::CalibrationState::GoWest, 0, 0.0 });

    int guard = 0;
    gc::CalibrationAction a;
    do
    {
        a = session.update(mount.pos);
        if (a.complete || a.failed)
            break;
        mount.pulse(a.direction, a.durationMs);
        path.push_back({ mount.pos.X, mount.pos.Y, a.direction, a.state, a.durationMs, a.dist });
    } while (++guard < 500);

    const gc::Calibration& cal = session.calibration();
    const double ortho = std::fabs(std::fabs(normAngle(cal.xAngle - cal.yAngle)) - PI / 2.0) * 180.0 / PI;

    std::printf("{\"start\":{\"x\":%.4f,\"y\":%.4f},", start.X, start.Y);
    std::printf("\"rate\":%.6f,\"westAngle\":%.6f,\"northAngle\":%.6f,", mount.ratePxPerMs, mount.westAngle,
                mount.northAngle);
    std::printf("\"complete\":%s,\"failed\":%s,\"pulses\":%d,", a.complete ? "true" : "false",
                a.failed ? "true" : "false", (int) path.size() - 1);
    std::printf("\"result\":{\"xAngle\":%.6f,\"yAngle\":%.6f,\"xRate\":%.6f,\"yRate\":%.6f,"
                "\"orthoErrorDeg\":%.4f,\"endX\":%.4f,\"endY\":%.4f,\"homeDist\":%.4f},",
                cal.xAngle, cal.yAngle, cal.xRate, cal.yRate, ortho, mount.pos.X, mount.pos.Y,
                mount.pos.Distance(start));

    std::printf("\"path\":[");
    for (size_t i = 0; i < path.size(); i++)
    {
        const Step& s = path[i];
        std::printf("%s{\"x\":%.4f,\"y\":%.4f,\"dir\":\"%s\",\"state\":\"%s\",\"ms\":%d,\"dist\":%.4f}", i ? "," : "",
                    s.x, s.y, dirName(s.dir), stateName(s.state), s.ms, s.dist);
    }
    std::printf("]}\n");
    return a.failed ? 1 : 0;
}
