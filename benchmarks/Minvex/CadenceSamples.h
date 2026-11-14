#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace benchmark {
// OnFrame precedes submission. A new frame number proves that the preceding
// frame completed; a retried callback must not produce another cadence sample.
class CadenceSamples {
public:
    CadenceSamples(std::uint64_t warmup, std::size_t frames) : warmup_(warmup), expected_(frames) {
        if (!frames) { throw std::invalid_argument("Benchmark needs at least one measured frame."); }
        intervals_.reserve(frames);
    }
    void Record(std::uint64_t frame, double nowMs) {
        if (!std::isfinite(nowMs) || nowMs < 0) { throw std::invalid_argument("Invalid benchmark clock."); }
        ++callbacks_;
        if (lastFrame_ && frame == *lastFrame_) { ++duplicates_; return; }
        if ((!lastFrame_ && frame != 0) || (lastFrame_ && frame != *lastFrame_ + 1)) {
            throw std::invalid_argument("Benchmark frame sequence is incomplete or moved backwards.");
        }
        if (lastFrame_ && nowMs <= previousMs_) { throw std::invalid_argument("Benchmark clock did not advance."); }
        if (frame > warmup_ && intervals_.size() < expected_) { intervals_.push_back(nowMs - previousMs_); }
        previousMs_ = nowMs;
        lastFrame_ = frame;
    }
    bool Complete() const noexcept { return intervals_.size() == expected_; }
    const std::vector<double>& Intervals() const noexcept { return intervals_; }
    std::size_t Callbacks() const noexcept { return callbacks_; }
    std::size_t DuplicateCallbacks() const noexcept { return duplicates_; }
private:
    std::uint64_t warmup_;
    std::size_t expected_, callbacks_ = 0, duplicates_ = 0;
    std::optional<std::uint64_t> lastFrame_;
    double previousMs_ = 0;
    std::vector<double> intervals_;
};
} // namespace benchmark
