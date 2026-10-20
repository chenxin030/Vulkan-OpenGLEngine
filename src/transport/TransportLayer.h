#pragma once

#include "core/Layer.h"
#include "core/Profiling.h"
#include "transport/TransportBenchmark.h"
#include "transport/TransportWorld.h"
#include "scene/CameraController.h"
#include <functional>
#include <unordered_map>

class Renderer;
class TransportLayer final : public Layer
{
public:
    TransportLayer(Renderer& renderer, TransportOptions options, std::function<void(bool)> finish);
    void OnDetach() noexcept override;
    void OnUpdate(FrameContext& frame) override;
    void OnImGuiRender(const FrameContext& frame) override;
    void OnFrameEnd() override;
    void OnEvent(Event& event) override;
private:
    void CollectGpu();
    void CompleteRun();
    void PrepareRun();
    Renderer& m_renderer;
    TransportOptions m_options;
    TransportWorld m_world;
    FixedTransportClock m_clock;
    CameraController m_camera;
    std::function<void(bool)> m_finish;
    std::vector<TransportFrame> m_frames;
    std::unordered_map<uint64_t, size_t> m_frameRows;
    TransportFrame m_current;
    RenderMetrics m_lastRender;
    double m_lastFrameMs = 0, m_lastGpuMs = -1;
    double m_setupMs = 0;
    bool m_skipDelta = true;
    ProfileClock::time_point m_frameStart;
    uint64_t m_samplingTick = 0, m_warmupTick = 0;
    uint32_t m_run = 1, m_drainFrames = 0;
    bool m_recordFrame = false, m_draining = false, m_finished = false;
    bool m_cameraGesture = false, m_cameraAdmitted = false;
    std::string m_invalid;
};
