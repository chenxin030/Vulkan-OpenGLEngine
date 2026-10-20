#include "transport/TransportLayer.h"
#include "editor/ViewportInteraction.h"
#include <cstdio>
#include "rhi/Renderer.h"
#include <imgui.h>
#include <iostream>
#include <cmath>

TransportLayer::TransportLayer(Renderer& renderer, TransportOptions options, std::function<void(bool)> finish)
    : m_renderer(renderer), m_options(std::move(options)), m_world(m_options.limit, m_options.interval), m_finish(std::move(finish))
{
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_options.output /= "session-" + std::to_string(stamp);
    std::cout << "[transport] " << (m_options.benchmark ? "benchmark" : "observe")
        << " limit=" << m_options.limit << " interval=" << m_world.Interval()
        << " output=" << m_options.output.string() << std::endl;
    if (m_options.benchmark) PrepareRun();
}
void TransportLayer::PrepareRun()
{
    ZoneScopedN("Transport.PrepareStableState");
    const auto start = ProfileClock::now();
    // Deterministic setup, outside the sampled region. Real-time warmup still follows.
    while (!m_world.Stable()) m_world.Tick();
    m_setupMs = ProfileMilliseconds(start);
    m_skipDelta = true;
    std::cout << "[transport] stabilized tick=" << m_world.Ticks() << " setup_ms=" << m_setupMs << std::endl;
}
void TransportLayer::OnDetach() noexcept
{
    if (!m_options.benchmark || m_finished) return;
    try {
        WriteTransportResults(m_options, m_renderer.DeviceInfo(), m_run, m_world.Interval(),
            m_frames, "application exited before benchmark completed", m_renderer.SkippedGpuSamples(), m_setupMs);
    } catch (const std::exception& error) { std::cerr << "[transport] incomplete run report: " << error.what() << '\n'; }
}
void TransportLayer::OnEvent(Event& event)
{
    if (event.type == EventType::WindowFocus && !event.focused) m_cameraGesture = false;
    if (!m_options.benchmark || m_finished) return;
    if (event.type == EventType::WindowFocus && !event.focused) m_invalid = "window lost focus";
    if (event.type == EventType::FramebufferResize && (event.width != 1280 || event.height != 720))
        m_invalid = "framebuffer resized or minimized";
    if (event.type == EventType::WindowClose) m_invalid = "window closed before benchmark completed";
    // A minimized application skips updates, so invalidate and write here as well.
    if (!m_invalid.empty()) {
        CollectGpu();
        WriteTransportResults(m_options, m_renderer.DeviceInfo(), m_run, m_world.Interval(),
            m_frames, m_invalid, m_renderer.SkippedGpuSamples(), m_setupMs);
        std::cerr << "[transport] invalid: " << m_invalid << std::endl;
        m_finished = true; m_finish(false);
    }
}
void TransportLayer::OnUpdate(FrameContext& frame)
{
    ZoneScopedN("Transport.UpdateAndSnapshot");
    m_frameStart = frame.frameStarted;
    m_current = {};
    m_recordFrame = false;
    if (m_options.benchmark && (frame.framebufferSize.x != 1280 || frame.framebufferSize.y != 720)) {
        Event resized{EventType::FramebufferResize};
        resized.width = int(frame.framebufferSize.x); resized.height = int(frame.framebufferSize.y);
        OnEvent(resized);
    }
    if (!m_options.benchmark) {
        m_cameraGesture = frame.input.lookActive;
        auto input = FilterViewportCamera(frame.input, {{0,0},frame.framebufferSize}, true,
            m_cameraGesture, false, m_cameraAdmitted);
        m_camera.Update(m_world.GetScene().camera, input, frame.deltaSeconds);
    }
    const auto updateStart = ProfileClock::now();
    if (!m_draining && !m_finished) {
        m_current.steps = m_clock.Advance(m_skipDelta ? 0 : frame.deltaSeconds);
        m_skipDelta = false;
        for (uint32_t i = 0; i < m_current.steps; ++i) m_world.Tick();
    }
    m_current.updateMs = ProfileMilliseconds(updateStart);
    m_current.backlogMs = std::floor((m_clock.BacklogSeconds() + 1e-12) / TransportWorld::Step) * TransportWorld::Step * 1000;
    m_current.active = m_world.Active(); m_current.tick = m_world.Ticks();
    m_current.spawned = m_world.Spawned(); m_current.arrived = m_world.Arrived();
    m_current.entities = uint32_t(m_world.GetScene().registry.EntityCount());
    m_current.meshes = m_world.GetScene().meshes.Count();
    m_current.materials = m_world.GetScene().materials.MaterialCount();
    m_current.textures = m_world.GetScene().materials.TextureCount();
    if (m_options.benchmark && !m_draining && !m_finished) {
        if (!m_warmupTick && m_world.Stable()) m_warmupTick = m_world.Ticks();
        if (m_warmupTick && !m_samplingTick &&
            m_world.Ticks() - m_warmupTick >= uint64_t(std::ceil(m_options.warmup * 60)))
            m_samplingTick = m_world.Ticks();
        if (m_samplingTick) {
            m_recordFrame = true;
            m_current.elapsed = double(m_world.Ticks() - m_samplingTick) / 60.0;
        }
    }
    {
        ZoneScopedN("Transport.BuildSnapshot");
        const auto start = ProfileClock::now();
        frame.Submit(BuildRenderSnapshot(m_world.GetScene()));
        m_current.snapshotMs = ProfileMilliseconds(start);
    }
    frame.viewportRequest = {m_options.benchmark ? ViewportExtent{1280,720} :
        ViewportExtent{uint32_t(frame.framebufferSize.x),uint32_t(frame.framebufferSize.y)}, true};
    TracyPlot("Cargo.Active", double(m_world.Active()));
    TracyPlot("Simulation.BacklogMs", m_current.backlogMs);
}
void TransportLayer::OnImGuiRender(const FrameContext& frame)
{
    // Presentation only: one background Image and one stats line. No editor/DockSpace.
    const auto size = ImGui::GetIO().DisplaySize;
    const auto& image = frame.viewportImage;
    if (m_options.benchmark && (!image.Valid() || image.extent != ViewportExtent{1280,720}))
        throw std::runtime_error("Benchmark scene target is unavailable or not 1280x720");
    if (image.Valid()) ImGui::GetBackgroundDrawList()->AddImage(ImTextureID(image.textureId), {0,0}, size,
        {image.uv0.x,image.uv0.y}, {image.uv1.x,image.uv1.y});
    std::string phase = m_draining ? "drain" : m_samplingTick ? "sample" : m_warmupTick ? "warmup" : "stabilize";
    char line[400];
    std::snprintf(line, sizeof(line), "%s | %s | cargo %u/%u | run %u | tick %llu | backlog %.2f ms | draws %llu+%llu | frame %.2f ms | GPU %.2f ms",
        m_options.backend.c_str(), m_options.benchmark ? phase.c_str() : "observe",
        m_world.Active(), m_options.limit, m_run, static_cast<unsigned long long>(m_world.Ticks()), m_current.backlogMs,
        static_cast<unsigned long long>(m_lastRender.sceneDraws), static_cast<unsigned long long>(m_lastRender.shadowDraws),
        m_lastFrameMs, m_lastGpuMs);
    ImGui::GetForegroundDrawList()->AddText({8,8}, IM_COL32_WHITE, line);
}
void TransportLayer::CollectGpu()
{
    for (const auto& sample : m_renderer.TakeGpuSamples()) {
        m_lastGpuMs = sample.shadowMs + sample.sceneMs + sample.toneMs;
        const auto row = m_frameRows.find(sample.frame);
        if (row != m_frameRows.end()) m_frames[row->second].gpu = sample;
        TracyPlot("GPU.ShadowMs", sample.shadowMs);
        TracyPlot("GPU.SceneMs", sample.sceneMs);
        TracyPlot("GPU.ToneMs", sample.toneMs);
        TracyPlot("GPU.UiMs", sample.uiMs);
    }
}
void TransportLayer::OnFrameEnd()
{
    ZoneScopedN("Transport.FrameEnd");
    const auto& metrics = m_renderer.Metrics();
    m_lastRender = metrics;
    m_lastFrameMs = ProfileMilliseconds(m_frameStart);
    if (m_recordFrame && metrics.submitted) {
        m_current.frame = metrics.frame; m_current.render = metrics;
        m_current.frameMs = ProfileMilliseconds(m_frameStart);
        m_frameRows.emplace(metrics.frame, m_frames.size());
        m_frames.push_back(m_current);
    }
    CollectGpu();
    FrameMark;
    if (m_finished) return;
    if (!m_options.benchmark) {
        if (m_options.observeSeconds > 0 && m_world.Ticks() * TransportWorld::Step >= m_options.observeSeconds) {
            m_finished = true; m_finish(true);
        }
        return;
    }
    if (m_recordFrame && m_current.elapsed >= m_options.seconds) m_draining = true;
    if (m_draining) {
        bool complete = true;
        if (m_renderer.DeviceInfo().gpuTimestamps)
            for (const auto& row : m_frames) if (!row.gpu) { complete = false; break; }
        if (complete || ++m_drainFrames >= 120) CompleteRun();
    }
}
void TransportLayer::CompleteRun()
{
    WriteTransportResults(m_options, m_renderer.DeviceInfo(), m_run, m_world.Interval(),
        m_frames, m_invalid, m_renderer.SkippedGpuSamples(), m_setupMs);
    std::cout << "[transport] run=" << m_run << " frames=" << m_frames.size()
        << " active=" << m_world.Active() << " present=" << m_renderer.DeviceInfo().presentMode << std::endl;
    if (++m_run > m_options.repeats) { m_finished = true; m_finish(true); return; }
    // Called after snapshot release. Keep resource identities/revisions stable between runs.
    m_world.Reset(); m_clock.Reset();
    m_warmupTick = m_samplingTick = 0; m_drainFrames = 0; m_draining = false;
    m_frames.clear(); m_frameRows.clear();
    PrepareRun();
}
