#pragma once
#include <minvex/Viewer.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>

namespace equipment {
// Opt-in, bounded capture of actual submitted-frame cadence and pose continuity.
class Diagnostics {
public:
    void Record(const lv::ViewerFrameInfo& frame, const lv::Scene& scene, lv::ObjectId body, lv::ObjectId clothes) {
        const auto now = std::chrono::steady_clock::now();
        const auto interval = previous_ ? std::chrono::duration<double, std::milli>(now - *previous_).count() : 0;
        previous_ = now; // Observe wall-clock cadence even when the simulation delta is clamped after a stall.
        if (frame.FrameNumber < 30 || samples_.size() >= 36000) { return; }
        const auto source = scene.EvaluatePose(body), follower = scene.EvaluatePose(clothes);
        double error = 0;
        for (std::size_t joint = 0; joint < 7; ++joint) { for (std::size_t i = 0; i < 16; ++i) {
            error = std::max(error, std::abs(double(source.Global[joint].Elements[i]) - follower.Global[joint].Elements[i]));
        } }
        const auto& matrix = source.Local[4].Elements;
        samples_.push_back({interval, scene.GetObject(body).Animation.Time(), std::atan2(double(matrix[1]), matrix[0]), error});
    }
    void Save(const std::filesystem::path& path, const lv::TelemetrySnapshot& telemetry, bool vsync, int fps) const {
        if (samples_.empty()) { throw std::runtime_error("Profile needs more than 30 frames (warmup)."); }
        std::vector<double> intervals; double error = 0; std::size_t over25 = 0;
        for (const auto& sample : samples_) { intervals.push_back(sample.Ms); error = std::max(error, sample.Error); over25 += sample.Ms > 25; }
        std::sort(intervals.begin(), intervals.end());
        const auto percentile = [&](double p) { return intervals[static_cast<std::size_t>(std::ceil(p * intervals.size())) - 1]; };
        const auto mean = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
        std::ofstream summary(path, std::ios::binary); summary << std::setprecision(12);
        summary << "{\n\"format\":\"MinvexEquipmentProfile\",\"version\":1,\"samples\":" << samples_.size()
            << ",\"vsync\":" << (vsync ? "true" : "false") << ",\"targetFps\":" << fps
            << ",\"meanFrameMs\":" << mean << ",\"averageFps\":" << 1000 / mean
            << ",\"p50FrameMs\":" << percentile(.5) << ",\"p95FrameMs\":" << percentile(.95) << ",\"maxFrameMs\":" << intervals.back()
            << ",\"framesOver25Ms\":" << over25 << ",\"maxFollowerMatrixError\":" << error << "\n}\n";
        summary.close(); if (!summary) { throw std::runtime_error("Cannot write equipment profile."); }
        auto csvPath = path; csvPath += ".csv";
        std::ofstream csv(csvPath, std::ios::binary); csv << std::setprecision(12) << "frameMs,clipSeconds,rightArmRadians,followerMatrixError\n";
        for (const auto& sample : samples_) { csv << sample.Ms << ',' << sample.Time << ',' << sample.Angle << ',' << sample.Error << '\n'; }
        csv.close(); if (!csv) { throw std::runtime_error("Cannot write equipment pose samples."); }
        auto telemetryPath = path; telemetryPath += ".telemetry.json";
        const auto saved = lv::SaveTelemetry(telemetry, telemetryPath); if (!saved) { throw std::runtime_error(saved.Error().Message); }
        std::printf("Equipment: %.1f FPS, mean %.2f ms, p95 %.2f ms, >25 ms %zu/%zu, follower error %.8g\n", 1000 / mean, mean, percentile(.95), over25, samples_.size(), error);
    }
private:
    struct Sample { double Ms, Time, Angle, Error; };
    std::vector<Sample> samples_;
    std::optional<std::chrono::steady_clock::time_point> previous_;
};
} // namespace equipment
