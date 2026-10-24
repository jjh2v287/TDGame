// 실행 환경: Windows 10 + Visual Studio 2022 cl.exe(vcvars64.bat), C++17 표준 라이브러리만, UE 빌드 불필요
// 실행 명령: python Tools/InfiniteWorld/fpdet/run_fpdet.py (변형 빌드·덤프·시간·비교 일괄) / 단독: chunkgen_port.exe dump <폴더> | time <json> [threads]
// 출력: dump=청크별 높이·온도·습도 float64 + 바이옴 uint8 원시 파일과 넓은 영역 높이·바이옴, time=단일 스레드 청크당 시간·다중 스레드 처리량 JSON
// 상태: 실험 재현용(2026-10-23 무한 샌드박스 세계 갈래 02 부동소수점 결정론), chunkgen.py 높이·기후 장 연산 순서 그대로 이식, 구조물 미이식

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using u64 = std::uint64_t;
using i64 = std::int64_t;

constexpr u64 WorldSeed = 20261023ull;
constexpr i64 UnitsPerMeter = 2;
constexpr u64 MixA = 0xBF58476D1CE4E5B9ull;
constexpr u64 MixB = 0x94D049BB133111EBull;
constexpr u64 LatticeKx = 0x9E3779B97F4A7C15ull;
constexpr u64 LatticeKy = 0xC2B2AE3D27D4EB4Full;
constexpr double Diagonal = 0.7071067811865476;
constexpr double GradientX[8] = {1.0, -1.0, 0.0, 0.0, Diagonal, -Diagonal, Diagonal, -Diagonal};
constexpr double GradientY[8] = {0.0, 0.0, 1.0, -1.0, Diagonal, Diagonal, -Diagonal, -Diagonal};
constexpr double NoiseScale = 1.4142135623730951;

constexpr u64 TagWarpX = 11;
constexpr u64 TagWarpY = 12;
constexpr u64 TagHeight = 13;
constexpr u64 TagContinent = 14;
constexpr u64 TagTemperature = 15;
constexpr u64 TagHumidity = 16;

constexpr i64 WarpCellUnits = 1024;
constexpr int WarpOctaves = 3;
constexpr double WarpAmplitudeM = 60.0;
constexpr i64 HeightCellUnits = 2048;
constexpr int HeightOctaves = 8;
constexpr i64 ContinentCellUnits = 16384;
constexpr int ContinentOctaves = 2;
constexpr i64 ClimateCellUnits = 16384;
constexpr int ClimateOctaves = 3;
constexpr double HeightBaseM = 20.0;
constexpr double HeightDetailM = 150.0;
constexpr double HeightContinentM = 90.0;
constexpr double TemperatureMeanC = 14.0;
constexpr double TemperatureRangeC = 60.0;
constexpr double TemperatureLapseCPerM = 0.08;
constexpr double HumidityRange = 2.0;
constexpr double AlpineHeightM = 85.0;
constexpr double TemperatureEdgesC[4] = {0.0, 7.0, 15.0, 22.0};
constexpr double HumidityEdges[3] = {0.2, 0.45, 0.7};
constexpr std::uint8_t WhittakerTable[5][4] = {
    {2, 2, 2, 2}, {4, 3, 3, 3}, {4, 4, 5, 6}, {7, 8, 5, 6}, {7, 8, 9, 10},
};

inline u64 Mix64(u64 value)
{
    value = (value ^ (value >> 30)) * MixA;
    value = (value ^ (value >> 27)) * MixB;
    return value ^ (value >> 31);
}

inline u64 DeriveSeed(std::initializer_list<u64> parts)
{
    u64 state = 0x243F6A8885A308D3ull;
    for (const u64 part : parts)
    {
        state = Mix64(state ^ part);
    }
    return state;
}

inline u64 HashLattice(u64 seed, i64 cellX, i64 cellY)
{
    const u64 hashed = Mix64(static_cast<u64>(cellX) * LatticeKx + seed);
    return Mix64(hashed ^ (static_cast<u64>(cellY) * LatticeKy));
}

inline i64 FloorDivide(i64 value, i64 divisor)
{
    const i64 quotient = value / divisor;
    const bool bHasRemainder = (value % divisor) != 0;
    const bool bSignsDiffer = (value < 0) != (divisor < 0);
    return (bHasRemainder && bSignsDiffer) ? quotient - 1 : quotient;
}

inline double Fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

inline double CornerDot(u64 seed, i64 cellX, i64 cellY, double offsetX, double offsetY)
{
    const int gradientIndex = static_cast<int>(HashLattice(seed, cellX, cellY) >> 61);
    return GradientX[gradientIndex] * offsetX + GradientY[gradientIndex] * offsetY;
}

inline double GradientNoise(u64 seed, i64 cellX, i64 cellY, double fracX, double fracY)
{
    const i64 nextX = cellX + 1;
    const i64 nextY = cellY + 1;
    const double backX = fracX - 1.0;
    const double backY = fracY - 1.0;
    const double n00 = CornerDot(seed, cellX, cellY, fracX, fracY);
    const double n10 = CornerDot(seed, nextX, cellY, backX, fracY);
    const double n01 = CornerDot(seed, cellX, nextY, fracX, backY);
    const double n11 = CornerDot(seed, nextX, nextY, backX, backY);
    const double blendX = Fade(fracX);
    const double blendY = Fade(fracY);
    const double bottom = n00 + blendX * (n10 - n00);
    const double top = n01 + blendX * (n11 - n01);
    return (bottom + blendY * (top - bottom)) * NoiseScale;
}

inline void ComputeLatticeCoordinate(i64 positionUnits, i64 cellUnits, i64& cell, double& frac)
{
    cell = FloorDivide(positionUnits, cellUnits);
    frac = static_cast<double>(positionUnits - cell * cellUnits) / static_cast<double>(cellUnits);
}

inline void ComputeWarpedLatticeCoordinate(i64 positionUnits, double warpM, i64 cellUnits, i64& cell, double& frac)
{
    ComputeLatticeCoordinate(positionUnits, cellUnits, cell, frac);
    frac = frac + warpM * (static_cast<double>(UnitsPerMeter) / static_cast<double>(cellUnits));
    const double carry = std::floor(frac);
    cell = cell + static_cast<i64>(carry);
    frac = frac - carry;
}

struct FTDFbmOctave
{
    u64 Seed;
    i64 OffsetX;
    i64 OffsetY;
    i64 CellUnits;
};

template <int OctaveCount>
struct FTDFbmLayer
{
    FTDFbmOctave Octaves[OctaveCount];
};

template <int OctaveCount>
FTDFbmLayer<OctaveCount> BuildFbmLayer(u64 layerSeed, i64 baseCellUnits)
{
    FTDFbmLayer<OctaveCount> layer{};
    i64 cellUnits = baseCellUnits;
    for (int octave = 0; octave < OctaveCount; ++octave)
    {
        const u64 octaveSeed = DeriveSeed({layerSeed, static_cast<u64>(octave)});
        layer.Octaves[octave].Seed = octaveSeed;
        layer.Octaves[octave].OffsetX = static_cast<i64>(DeriveSeed({octaveSeed, 1}) % 65536);
        layer.Octaves[octave].OffsetY = static_cast<i64>(DeriveSeed({octaveSeed, 2}) % 65536);
        layer.Octaves[octave].CellUnits = cellUnits;
        cellUnits /= 2;
    }
    return layer;
}

template <int OctaveCount>
double EvaluateFbm(const FTDFbmLayer<OctaveCount>& layer, i64 positionX, i64 positionY)
{
    double total = 0.0;
    double amplitude = 1.0;
    double amplitudeSum = 0.0;
    for (int octave = 0; octave < OctaveCount; ++octave)
    {
        const FTDFbmOctave& entry = layer.Octaves[octave];
        i64 cellX, cellY;
        double fracX, fracY;
        ComputeLatticeCoordinate(positionX + entry.OffsetX, entry.CellUnits, cellX, fracX);
        ComputeLatticeCoordinate(positionY + entry.OffsetY, entry.CellUnits, cellY, fracY);
        total += amplitude * GradientNoise(entry.Seed, cellX, cellY, fracX, fracY);
        amplitudeSum += amplitude;
        amplitude *= 0.5;
    }
    return total / amplitudeSum;
}

template <int OctaveCount>
double EvaluateWarpedFbm(const FTDFbmLayer<OctaveCount>& layer, i64 positionX, i64 positionY, double warpX, double warpY)
{
    double total = 0.0;
    double amplitude = 1.0;
    double amplitudeSum = 0.0;
    for (int octave = 0; octave < OctaveCount; ++octave)
    {
        const FTDFbmOctave& entry = layer.Octaves[octave];
        i64 cellX, cellY;
        double fracX, fracY;
        ComputeWarpedLatticeCoordinate(positionX + entry.OffsetX, warpX, entry.CellUnits, cellX, fracX);
        ComputeWarpedLatticeCoordinate(positionY + entry.OffsetY, warpY, entry.CellUnits, cellY, fracY);
        total += amplitude * GradientNoise(entry.Seed, cellX, cellY, fracX, fracY);
        amplitudeSum += amplitude;
        amplitude *= 0.5;
    }
    return total / amplitudeSum;
}

struct FTDWorldLayers
{
    FTDFbmLayer<WarpOctaves> WarpX;
    FTDFbmLayer<WarpOctaves> WarpY;
    FTDFbmLayer<HeightOctaves> Height;
    FTDFbmLayer<ContinentOctaves> Continent;
    FTDFbmLayer<ClimateOctaves> Temperature;
    FTDFbmLayer<ClimateOctaves> Humidity;
};

FTDWorldLayers BuildWorldLayers(u64 worldSeed)
{
    FTDWorldLayers layers;
    layers.WarpX = BuildFbmLayer<WarpOctaves>(DeriveSeed({worldSeed, TagWarpX}), WarpCellUnits);
    layers.WarpY = BuildFbmLayer<WarpOctaves>(DeriveSeed({worldSeed, TagWarpY}), WarpCellUnits);
    layers.Height = BuildFbmLayer<HeightOctaves>(DeriveSeed({worldSeed, TagHeight}), HeightCellUnits);
    layers.Continent = BuildFbmLayer<ContinentOctaves>(DeriveSeed({worldSeed, TagContinent}), ContinentCellUnits);
    layers.Temperature = BuildFbmLayer<ClimateOctaves>(DeriveSeed({worldSeed, TagTemperature}), ClimateCellUnits);
    layers.Humidity = BuildFbmLayer<ClimateOctaves>(DeriveSeed({worldSeed, TagHumidity}), ClimateCellUnits);
    return layers;
}

inline double EvaluateBaseHeight(const FTDWorldLayers& layers, i64 positionX, i64 positionY)
{
    const double warpX = WarpAmplitudeM * EvaluateFbm(layers.WarpX, positionX, positionY);
    const double warpY = WarpAmplitudeM * EvaluateFbm(layers.WarpY, positionX, positionY);
    const double detail = EvaluateWarpedFbm(layers.Height, positionX, positionY, warpX, warpY);
    const double continent = EvaluateFbm(layers.Continent, positionX, positionY);
    return HeightBaseM + HeightDetailM * detail + HeightContinentM * continent;
}

template <int EdgeCount>
inline int CountEdgesAtOrBelow(const double (&edges)[EdgeCount], double value)
{
    int count = 0;
    for (int index = 0; index < EdgeCount; ++index)
    {
        count += (edges[index] <= value) ? 1 : 0;
    }
    return count;
}

struct FTDClimateSample
{
    double Temperature;
    double Humidity;
    std::uint8_t Biome;
};

inline FTDClimateSample ClassifyClimate(const FTDWorldLayers& layers, i64 positionX, i64 positionY, double baseHeight)
{
    double temperature = TemperatureMeanC + TemperatureRangeC * EvaluateFbm(layers.Temperature, positionX, positionY);
    temperature = temperature - TemperatureLapseCPerM * std::max(baseHeight, 0.0);
    const double rawHumidity = 0.5 + HumidityRange * EvaluateFbm(layers.Humidity, positionX, positionY);
    const double humidity = std::min(std::max(rawHumidity, 0.0), 1.0);
    std::uint8_t biome = WhittakerTable[CountEdgesAtOrBelow(TemperatureEdgesC, temperature)][CountEdgesAtOrBelow(HumidityEdges, humidity)];
    if (baseHeight > AlpineHeightM)
    {
        biome = 1;
    }
    if (baseHeight < 0.0)
    {
        biome = 0;
    }
    return {temperature, humidity, biome};
}

struct FTDChunkSpec
{
    i64 ChunkX;
    i64 ChunkY;
    i64 ChunkMeters;
    i64 StepUnits;

    i64 CountIntervals() const { return ChunkMeters * UnitsPerMeter / StepUnits; }
    i64 CountAxisSamples() const { return CountIntervals() + 1; }
    i64 ComputeOriginUnits(i64 chunkIndex) const { return chunkIndex * CountIntervals() * StepUnits; }
};

struct FTDChunkBuffers
{
    std::vector<double> Height;
    std::vector<double> Temperature;
    std::vector<double> Humidity;
    std::vector<std::uint8_t> Biome;

    void Resize(std::size_t sampleCount)
    {
        Height.resize(sampleCount);
        Temperature.resize(sampleCount);
        Humidity.resize(sampleCount);
        Biome.resize(sampleCount);
    }
};

void GenerateChunk(const FTDWorldLayers& layers, const FTDChunkSpec& spec, bool bWithClimate, FTDChunkBuffers& buffers)
{
    const i64 axisSamples = spec.CountAxisSamples();
    const i64 originX = spec.ComputeOriginUnits(spec.ChunkX);
    const i64 originY = spec.ComputeOriginUnits(spec.ChunkY);
    buffers.Resize(static_cast<std::size_t>(axisSamples * axisSamples));
    for (i64 row = 0; row < axisSamples; ++row)
    {
        const i64 positionY = originY + row * spec.StepUnits;
        for (i64 column = 0; column < axisSamples; ++column)
        {
            const i64 positionX = originX + column * spec.StepUnits;
            const std::size_t index = static_cast<std::size_t>(row * axisSamples + column);
            const double baseHeight = EvaluateBaseHeight(layers, positionX, positionY);
            buffers.Height[index] = baseHeight;
            if (!bWithClimate)
            {
                continue;
            }
            const FTDClimateSample climate = ClassifyClimate(layers, positionX, positionY, baseHeight);
            buffers.Temperature[index] = climate.Temperature;
            buffers.Humidity[index] = climate.Humidity;
            buffers.Biome[index] = climate.Biome;
        }
    }
}

bool WriteBytes(const std::string& path, const std::vector<const void*>& blocks, const std::vector<std::size_t>& sizes)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
    {
        return false;
    }
    for (std::size_t index = 0; index < blocks.size(); ++index)
    {
        std::fwrite(blocks[index], 1, sizes[index], file);
    }
    std::fclose(file);
    return true;
}

int RunDump(const std::string& outputDirectory)
{
    const FTDWorldLayers layers = BuildWorldLayers(WorldSeed);
    const i64 chunkCoordinates[4][2] = {{0, 0}, {-1, 0}, {10000000, -3000000}, {i64(1) << 40, 5}};
    const i64 stepChoices[2] = {2, 1};
    FTDChunkBuffers buffers;
    for (const auto& coordinate : chunkCoordinates)
    {
        for (const i64 stepUnits : stepChoices)
        {
            const FTDChunkSpec spec{coordinate[0], coordinate[1], 64, stepUnits};
            GenerateChunk(layers, spec, true, buffers);
            const std::size_t count = buffers.Height.size();
            const std::string path = outputDirectory + "/c_" + std::to_string(coordinate[0]) + "_" + std::to_string(coordinate[1]) +
                                     "_64m_" + std::to_string(stepUnits) + "u.bin";
            if (!WriteBytes(path, {buffers.Height.data(), buffers.Temperature.data(), buffers.Humidity.data(), buffers.Biome.data()},
                            {count * 8, count * 8, count * 8, count}))
            {
                std::fprintf(stderr, "write failed: %s\n", path.c_str());
                return 2;
            }
        }
    }
    std::vector<double> wideHeight;
    std::vector<std::uint8_t> wideBiome;
    for (i64 chunkY = -4; chunkY < 4; ++chunkY)
    {
        for (i64 chunkX = -4; chunkX < 4; ++chunkX)
        {
            GenerateChunk(layers, FTDChunkSpec{chunkX, chunkY, 64, 1}, true, buffers);
            wideHeight.insert(wideHeight.end(), buffers.Height.begin(), buffers.Height.end());
            wideBiome.insert(wideBiome.end(), buffers.Biome.begin(), buffers.Biome.end());
        }
    }
    const std::string widePath = outputDirectory + "/wide_8x8_64m_1u.bin";
    if (!WriteBytes(widePath, {wideHeight.data(), wideBiome.data()}, {wideHeight.size() * 8, wideBiome.size()}))
    {
        std::fprintf(stderr, "write failed: %s\n", widePath.c_str());
        return 2;
    }
    return 0;
}

struct FTDTimingStats
{
    double MedianMs;
    double P10Ms;
    double P90Ms;
    double MeanMs;
};

FTDTimingStats SummarizeMilliseconds(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    double sum = 0.0;
    for (const double value : values)
    {
        sum += value;
    }
    const double median = (count % 2 == 1) ? values[count / 2] : 0.5 * (values[count / 2 - 1] + values[count / 2]);
    return {median, values[count / 10], values[(count * 9) / 10], sum / static_cast<double>(count)};
}

double SumHeights(const FTDChunkBuffers& buffers)
{
    double sum = 0.0;
    for (const double value : buffers.Height)
    {
        sum += value;
    }
    return sum;
}

int RunTiming(const std::string& outputPath, bool bWithThreads)
{
    using Clock = std::chrono::steady_clock;
    const FTDWorldLayers layers = BuildWorldLayers(WorldSeed);
    struct FTDConfig
    {
        const char* Name;
        i64 ChunkMeters;
        i64 StepUnits;
        int Count;
    };
    const FTDConfig configs[4] = {{"32m@1m", 32, 2, 301}, {"64m@1m", 64, 2, 201}, {"64m@0.5m", 64, 1, 81}, {"128m@1m", 128, 2, 81}};
    double checksum = 0.0;
    FILE* file = std::fopen(outputPath.c_str(), "w");
    if (!file)
    {
        return 2;
    }
    std::fprintf(file, "{\n  \"single\": [\n");
    bool bIsFirstEntry = true;
    for (const bool bWithClimate : {false, true})
    {
        for (const FTDConfig& config : configs)
        {
            FTDChunkBuffers buffers;
            for (int warmup = 0; warmup < 3; ++warmup)
            {
                GenerateChunk(layers, FTDChunkSpec{-100 - warmup, 9, config.ChunkMeters, config.StepUnits}, bWithClimate, buffers);
            }
            std::vector<double> milliseconds;
            for (int index = 0; index < config.Count; ++index)
            {
                const FTDChunkSpec spec{index - config.Count / 2, 3 + (index % 5), config.ChunkMeters, config.StepUnits};
                const auto begin = Clock::now();
                GenerateChunk(layers, spec, bWithClimate, buffers);
                const auto end = Clock::now();
                checksum += SumHeights(buffers);
                milliseconds.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
            }
            const FTDTimingStats stats = SummarizeMilliseconds(milliseconds);
            const i64 axisSamples = FTDChunkSpec{0, 0, config.ChunkMeters, config.StepUnits}.CountAxisSamples();
            const double sampleCount = static_cast<double>(axisSamples * axisSamples);
            std::fprintf(file,
                         "%s    {\"config\": \"%s\", \"mode\": \"%s\", \"samples\": %lld, \"count\": %d, \"median_ms\": %.4f, \"p10_ms\": %.4f, "
                         "\"p90_ms\": %.4f, \"mean_ms\": %.4f, \"ns_per_sample\": %.1f}",
                         bIsFirstEntry ? "" : ",\n", config.Name, bWithClimate ? "height+climate" : "height", static_cast<long long>(sampleCount),
                         config.Count, stats.MedianMs, stats.P10Ms, stats.P90Ms, stats.MeanMs, stats.MedianMs * 1.0e6 / sampleCount);
            bIsFirstEntry = false;
        }
    }
    std::fprintf(file, "\n  ],\n  \"threads\": [\n");
    bIsFirstEntry = true;
    if (bWithThreads)
    {
        struct FTDThroughputConfig
        {
            const char* Name;
            i64 StepUnits;
            int TotalChunks;
        };
        const FTDThroughputConfig throughputConfigs[2] = {{"64m@1m", 2, 1600}, {"64m@0.5m", 1, 400}};
        const unsigned int threadCounts[5] = {1, 2, 4, 8, 16};
        for (const FTDThroughputConfig& config : throughputConfigs)
        {
            for (const unsigned int threadCount : threadCounts)
            {
                std::atomic<int> nextChunk{0};
                std::vector<double> threadChecksums(threadCount, 0.0);
                const auto begin = Clock::now();
                std::vector<std::thread> workers;
                for (unsigned int worker = 0; worker < threadCount; ++worker)
                {
                    workers.emplace_back([&, worker]() {
                        FTDChunkBuffers buffers;
                        for (int chunk = nextChunk.fetch_add(1); chunk < config.TotalChunks; chunk = nextChunk.fetch_add(1))
                        {
                            GenerateChunk(layers, FTDChunkSpec{chunk, -11, 64, config.StepUnits}, true, buffers);
                            threadChecksums[worker] += SumHeights(buffers);
                        }
                    });
                }
                for (std::thread& worker : workers)
                {
                    worker.join();
                }
                const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
                for (const double value : threadChecksums)
                {
                    checksum += value;
                }
                std::fprintf(file, "%s    {\"config\": \"%s\", \"mode\": \"height+climate\", \"threads\": %u, \"chunks\": %d, \"wall_s\": %.4f, "
                                   "\"chunks_per_s\": %.1f, \"ms_per_chunk_effective\": %.4f}",
                             bIsFirstEntry ? "" : ",\n", config.Name, threadCount, config.TotalChunks, seconds,
                             config.TotalChunks / seconds, seconds * 1000.0 / config.TotalChunks);
                bIsFirstEntry = false;
            }
        }
    }
    std::fprintf(file, "\n  ],\n  \"checksum\": %.6f\n}\n", checksum);
    std::fclose(file);
    return 0;
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount < 3)
    {
        std::fprintf(stderr, "usage: chunkgen_port dump <dir> | time <json> [threads]\n");
        return 1;
    }
    const std::string mode = arguments[1];
    if (mode == "dump")
    {
        return RunDump(arguments[2]);
    }
    if (mode == "time")
    {
        const bool bWithThreads = argumentCount > 3 && std::strcmp(arguments[3], "threads") == 0;
        return RunTiming(arguments[2], bWithThreads);
    }
    std::fprintf(stderr, "unknown mode\n");
    return 1;
}
