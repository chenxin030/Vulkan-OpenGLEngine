#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Identifiers refer to submitted frames, not the later frame which reads a query.
struct GpuSample
{
    uint64_t frame = 0;
    double shadowMs = 0, sceneMs = 0, toneMs = 0, uiMs = 0;
};
struct RenderMetrics
{
    uint64_t frame = 0;
    bool submitted = false;
    uint64_t sceneDraws = 0, shadowDraws = 0, sceneTriangles = 0, shadowTriangles = 0;
    double fenceMs = 0, acquireMs = 0, presentMs = 0;
    double renderCpuMs = 0;
};
struct RenderConfiguration
{
    bool transport = false;
    bool uncapped = false;
};
struct RenderDeviceInfo
{
    std::string backend, device, presentMode = "unknown";
    bool presentationLimited = false;
    bool gpuTimestamps = false;
    bool validation = false;
};
