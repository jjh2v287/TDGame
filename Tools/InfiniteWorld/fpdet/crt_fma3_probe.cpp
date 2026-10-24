// 실행 환경: Windows 10 x64 + Visual Studio 2022 cl.exe(vcvars64.bat), C++17, UE 빌드 불필요, FMA3 지원 CPU에서 의미 있음
// 실행 명령: cl /nologo /std:c++17 /EHsc /O2 /fp:precise crt_fma3_probe.cpp 후 crt_fma3_probe.exe <출력 json>
// 출력: 같은 실행 파일에서 CRT 초월 함수의 FMA3 경로를 켰을 때와 껐을 때(_set_FMA3_enable) 결과 비트가 다른 표본 수 JSON
// 상태: 실험 재현용(2026-10-23 무한 샌드박스 세계 갈래 02), /fp:precise 만으로 CPU 간 결정론이 보장되는지 확인용

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <math.h>
#include <vector>

using u64 = std::uint64_t;

u64 NextRandom(u64& state)
{
    state += 0x9E3779B97F4A7C15ull;
    u64 value = state;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

double DrawUniform(u64& state, double low, double high)
{
    return low + (high - low) * (static_cast<double>(NextRandom(state) >> 11) * (1.0 / 9007199254740992.0));
}

struct FTDProbe
{
    const char* Name;
    double (*Evaluate)(double, double);
    double LowA, HighA, LowB, HighB;
};

double EvaluateSin(double a, double) { return std::sin(a); }
double EvaluateCos(double a, double) { return std::cos(a); }
double EvaluateTan(double a, double) { return std::tan(a); }
double EvaluateExp(double a, double) { return std::exp(a); }
double EvaluateLog(double a, double) { return std::log(a); }
double EvaluatePow(double a, double b) { return std::pow(a, b); }
double EvaluateAtan2(double a, double b) { return std::atan2(a, b); }
double EvaluateSqrt(double a, double) { return std::sqrt(a); }
double EvaluateSinFloat(double a, double) { return static_cast<double>(std::sin(static_cast<float>(a))); }
double EvaluateExpFloat(double a, double) { return static_cast<double>(std::exp(static_cast<float>(a))); }
double EvaluatePowFloat(double a, double b) { return static_cast<double>(std::pow(static_cast<float>(a), static_cast<float>(b))); }

std::vector<double> EvaluateAll(const FTDProbe& probe, int sampleCount)
{
    std::vector<double> results(static_cast<std::size_t>(sampleCount));
    u64 state = 20261023ull;
    for (int index = 0; index < sampleCount; ++index)
    {
        const double a = DrawUniform(state, probe.LowA, probe.HighA);
        const double b = DrawUniform(state, probe.LowB, probe.HighB);
        results[static_cast<std::size_t>(index)] = probe.Evaluate(a, b);
    }
    return results;
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount < 2)
    {
        std::fprintf(stderr, "usage: crt_fma3_probe <out.json>\n");
        return 1;
    }
    const FTDProbe probes[] = {
        {"sin", EvaluateSin, -1000.0, 1000.0, 0, 0},     {"cos", EvaluateCos, -1000.0, 1000.0, 0, 0},
        {"tan", EvaluateTan, -10.0, 10.0, 0, 0},         {"exp", EvaluateExp, -50.0, 50.0, 0, 0},
        {"log", EvaluateLog, 1.0e-6, 1.0e6, 0, 0},       {"pow", EvaluatePow, 0.1, 10.0, -5.0, 5.0},
        {"atan2", EvaluateAtan2, -10.0, 10.0, -10.0, 10.0}, {"sqrt", EvaluateSqrt, 0.0, 1.0e6, 0, 0},
        {"sinf", EvaluateSinFloat, -1000.0, 1000.0, 0, 0}, {"expf", EvaluateExpFloat, -50.0, 50.0, 0, 0},
        {"powf", EvaluatePowFloat, 0.1, 10.0, -5.0, 5.0},
    };
    const int sampleCount = 1000000;
    FILE* file = std::fopen(arguments[1], "w");
    if (!file)
    {
        return 2;
    }
    const int initialFlag = _get_FMA3_enable();
    std::fprintf(file, "{\n  \"initial_fma3_enable\": %d,\n  \"samples\": %d,\n  \"functions\": [\n", initialFlag, sampleCount);
    bool bIsFirst = true;
    for (const FTDProbe& probe : probes)
    {
        _set_FMA3_enable(1);
        const std::vector<double> withFma = EvaluateAll(probe, sampleCount);
        _set_FMA3_enable(0);
        const std::vector<double> withoutFma = EvaluateAll(probe, sampleCount);
        int differing = 0;
        double maxRelative = 0.0;
        for (int index = 0; index < sampleCount; ++index)
        {
            const double first = withFma[static_cast<std::size_t>(index)];
            const double second = withoutFma[static_cast<std::size_t>(index)];
            if (std::memcmp(&first, &second, sizeof(double)) == 0)
            {
                continue;
            }
            ++differing;
            const double scale = std::fabs(second) > 0.0 ? std::fabs(second) : 1.0;
            const double relative = std::fabs(first - second) / scale;
            maxRelative = relative > maxRelative ? relative : maxRelative;
        }
        std::fprintf(file, "%s    {\"name\": \"%s\", \"differing\": %d, \"max_relative_diff\": %.3e}", bIsFirst ? "" : ",\n", probe.Name, differing, maxRelative);
        bIsFirst = false;
    }
    _set_FMA3_enable(initialFlag);
    std::fprintf(file, "\n  ]\n}\n");
    std::fclose(file);
    return 0;
}
