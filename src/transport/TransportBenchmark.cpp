#include "transport/TransportBenchmark.h"
#include "transport/TransportWorld.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <thread>

namespace {
double Number(const std::string& text, double minimum, double maximum)
{
    size_t end = 0;
    const double value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value) || value < minimum || value > maximum)
        throw std::invalid_argument("Invalid transport numeric argument: " + text);
    return value;
}
uint32_t Integer(const std::string& text, uint32_t minimum, uint32_t maximum)
{
    const double value = Number(text, minimum, maximum);
    if (value != std::floor(value)) throw std::invalid_argument("Expected an integer: " + text);
    return uint32_t(value);
}
}
const char* TransportUsage()
{
    return "Rebuild [vulkan|opengl] --transport [--benchmark] [--limit 256|1024|4096] "
        "[--interval seconds] [--warmup 10] [--seconds 30] [--repeats 3] "
        "[--output directory] [--observe-seconds seconds]";
}
TransportOptions ParseTransportOptions(int argc, char** argv)
{
    TransportOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "opengl" || arg == "vulkan") { options.backend = arg; continue; }
        if (arg == "--transport") { options.enabled = true; continue; }
        if (arg == "--benchmark") { options.benchmark = true; continue; }
        if (arg == "--help") throw std::invalid_argument(TransportUsage());
        if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + arg);
        const std::string value = argv[++i];
        if (arg == "--limit") options.limit = Integer(value, 1, 100000);
        else if (arg == "--repeats") options.repeats = Integer(value, 1, 100);
        else if (arg == "--interval") options.interval = Number(value, 0, 3600);
        else if (arg == "--warmup") options.warmup = Number(value, 0, 3600);
        else if (arg == "--seconds") options.seconds = Number(value, 0.1, 3600);
        else if (arg == "--observe-seconds") options.observeSeconds = Number(value, 0.1, 3600);
        else if (arg == "--output") options.output = std::filesystem::u8path(value);
        else throw std::invalid_argument("Unknown argument: " + arg);
    }
    if (options.benchmark && !options.enabled) throw std::invalid_argument("--benchmark requires --transport");
    return options;
}
uint32_t FixedTransportClock::Advance(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid frame delta");
    m_accumulator += seconds;
    uint32_t steps = 0;
    while (m_accumulator + 1e-12 >= TransportWorld::Step && steps < 8) {
        m_accumulator -= TransportWorld::Step;
        ++steps;
    }
    m_accumulator = std::max(0.0, m_accumulator);
    return steps;
}
double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty()) return 0;
    if (!std::isfinite(percentile) || percentile < 0 || percentile > 1)
        throw std::invalid_argument("Percentile must be within 0..1");
    std::sort(values.begin(), values.end());
    if (percentile == 0) return values.front();
    return values[std::min(values.size() - 1, size_t(std::ceil(percentile * values.size()) - 1))];
}
void WriteTransportResults(const TransportOptions& options, const RenderDeviceInfo& device,
    uint32_t run, double interval, const std::vector<TransportFrame>& frames,
    const std::string& invalidReason, uint64_t droppedGpuSamples, double setupCpuMs)
{
    std::filesystem::create_directories(options.output);
    const auto stem = options.output / (options.backend + "-" + std::to_string(options.limit) + "-run" + std::to_string(run));
    std::ofstream csv(std::filesystem::path(stem).concat(".csv"));
    if (!csv) throw std::runtime_error("Cannot create benchmark CSV");
    csv << "frame,tick,elapsed_s,frame_ms,update_ms,snapshot_ms,render_cpu_ms,fence_ms,acquire_ms,present_ms,"
        "backlog_ms,steps,active,spawned,arrived,scene_draws,shadow_draws,scene_triangles,shadow_triangles,"
        "gpu_shadow_ms,gpu_scene_ms,gpu_tone_ms,gpu_ui_ms,entities,meshes,materials,textures\n";
    csv << std::fixed << std::setprecision(6);
    std::vector<double> times, updates, snapshots, renderCpu, gpuTimes;
    uint32_t minActive = UINT32_MAX, maxActive = 0;
    double maxBacklog = 0;
    for (const auto& frame : frames) {
        const auto& r = frame.render;
        csv << frame.frame << ',' << frame.tick << ',' << frame.elapsed << ',' << frame.frameMs << ','
            << frame.updateMs << ',' << frame.snapshotMs << ',' << r.renderCpuMs << ',' << r.fenceMs << ','
            << r.acquireMs << ',' << r.presentMs << ',' << frame.backlogMs << ',' << frame.steps << ','
            << frame.active << ',' << frame.spawned << ',' << frame.arrived << ',' << r.sceneDraws << ','
            << r.shadowDraws << ',' << r.sceneTriangles << ',' << r.shadowTriangles << ',';
        if (frame.gpu) {
            const auto& g = *frame.gpu;
            csv << g.shadowMs << ',' << g.sceneMs << ',' << g.toneMs << ',' << g.uiMs;
            gpuTimes.push_back(g.shadowMs + g.sceneMs + g.toneMs);
        } else csv << ",,,";
        csv << ',' << frame.entities << ',' << frame.meshes << ',' << frame.materials << ',' << frame.textures << '\n';
        times.push_back(frame.frameMs); updates.push_back(frame.updateMs);
        snapshots.push_back(frame.snapshotMs); renderCpu.push_back(r.renderCpuMs);
        minActive = std::min(minActive, frame.active); maxActive = std::max(maxActive, frame.active);
        maxBacklog = std::max(maxBacklog, frame.backlogMs);
    }
    csv.close();
    if (!csv) throw std::runtime_error("Benchmark CSV write failed");
    const auto stats = [](const std::vector<double>& values) -> nlohmann::json {
        if (values.empty()) return nullptr;
        return {{"median_ms", Percentile(values,0.5)}, {"p95_ms", Percentile(values,0.95)}, {"p99_ms", Percentile(values,0.99)}};
    };
    nlohmann::json summary = {
        {"backend",device.backend},{"device",device.device},{"present_mode",device.presentMode},
        {"presentation_limited",device.presentationLimited},{"validation",device.validation},
        {"gpu_timestamps",device.gpuTimestamps},{"run",run},{"limit",options.limit},{"interval_s",interval},
        {"fixed_step_s",TransportWorld::Step},{"width",1280},{"height",720},
        {"warmup_simulation_s",options.warmup},{"sample_simulation_s",options.seconds},
        {"stable_state_setup_cpu_ms",setupCpuMs},
        {"stabilization","Deterministically pre-simulate two longest flights, then start real-time warmup"},
        {"presentation_verification",device.backend == "opengl" ? "Swap interval 0 requested; driver overrides are not verified" : "Reported Vulkan present mode"},
        {"valid",invalidReason.empty() && !frames.empty()},{"invalid_reason",invalidReason},
        {"sampled_frames",frames.size()},{"gpu_samples",gpuTimes.size()},{"gpu_queries_skipped",droppedGpuSamples},
        {"active_min",frames.empty()?0:minActive},{"active_max",maxActive},{"max_backlog_ms",maxBacklog},
        {"logical_cpu_count", std::thread::hardware_concurrency()},
        {"resource_counts",frames.empty() ? nlohmann::json(nullptr) : nlohmann::json{
            {"meshes",frames.front().meshes},{"materials",frames.front().materials},{"textures",frames.front().textures}}},
        {"frame",stats(times)},{"update",stats(updates)},{"snapshot",stats(snapshots)},
        {"render_cpu_including_waits",stats(renderCpu)},{"gpu_shadow_scene_tone",stats(gpuTimes)},
#ifdef NDEBUG
        {"optimized",true},
#else
        {"optimized",false},
#endif
#ifdef TRACY_ENABLE
        {"tracy_enabled",true},
#else
        {"tracy_enabled",false},
#endif
        {"notes","CPU and GPU overlap; do not add their times. GPU rows use original submitted-frame IDs. Empty GPU fields are unavailable, not zero."}
    };
    std::ofstream json(std::filesystem::path(stem).concat(".json"));
    json << summary.dump(2) << '\n'; json.close();
    if (!json) throw std::runtime_error("Benchmark summary write failed");
}
