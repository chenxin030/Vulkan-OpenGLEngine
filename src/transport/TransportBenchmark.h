#pragma once

#include "rhi/RenderMetrics.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct TransportOptions
{
    bool enabled = false, benchmark = false;
    std::string backend = "vulkan";
    uint32_t limit = 1024, repeats = 3;
    double interval = 0, warmup = 10, seconds = 30, observeSeconds = 0;
    std::filesystem::path output = "transport-results";
};
TransportOptions ParseTransportOptions(int argc, char** argv);
const char* TransportUsage();

// Catch up in bounded batches without discarding accumulated simulation time.
class FixedTransportClock
{
public:
    uint32_t Advance(double seconds);
    double BacklogSeconds() const { return m_accumulator; }
    void Reset() { m_accumulator = 0; }
private:
    double m_accumulator = 0;
};
struct TransportFrame
{
    uint64_t frame = 0, tick = 0, spawned = 0, arrived = 0;
    uint32_t active = 0, steps = 0;
    uint32_t entities = 0, meshes = 0, materials = 0, textures = 0;
    double elapsed = 0, frameMs = 0, updateMs = 0, snapshotMs = 0, backlogMs = 0;
    RenderMetrics render;
    std::optional<GpuSample> gpu;
};
double Percentile(std::vector<double> values, double percentile);
void WriteTransportResults(const TransportOptions& options, const RenderDeviceInfo& device,
    uint32_t run, double interval, const std::vector<TransportFrame>& frames,
    const std::string& invalidReason, uint64_t droppedGpuSamples, double setupCpuMs = 0);
