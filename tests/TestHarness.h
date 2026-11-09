#pragma once

#include <minuvex/Asset.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace test {
inline int Checks = 0, Failures = 0;
inline void Check(bool condition, const char* message) {
    ++Checks;
    if (!condition) { ++Failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
inline bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
template<typename Function>
void Rejects(Function function, const char* message) {
    try { function(); Check(false, message); }
    catch (const std::invalid_argument&) { Check(true, message); }
}
template<typename Function>
void ImportRejects(Function function, lv::ImportErrorCode code, const char* message) {
    try { function(); Check(false, message); }
    catch (const lv::ImportError& error) { Check(error.Code == code && *error.what(), message); }
}
inline std::filesystem::path Fixture(const char* name) { return std::filesystem::path(MINUVEX_FIXTURE_DIR) / name; }
inline std::string Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { throw std::runtime_error("Cannot read test fixture."); }
    return {std::istreambuf_iterator<char>(stream), {}};
}
inline void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) { throw std::runtime_error("Cannot write test fixture."); }
}
inline std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const auto pos = text.find(from);
    if (pos == std::string::npos) { throw std::runtime_error("Test fixture replacement was not found: " + from); }
    text.replace(pos, from.size(), to);
    return text;
}
struct TemporaryDirectory {
    std::filesystem::path Path;
    TemporaryDirectory() {
        Path = std::filesystem::absolute(std::filesystem::path(MINUVEX_TEST_OUTPUT_DIR) /
            ("phase2-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).lexically_normal();
        std::filesystem::create_directories(Path);
    }
    ~TemporaryDirectory() {
        const auto parent = std::filesystem::absolute(MINUVEX_TEST_OUTPUT_DIR).lexically_normal();
        if (Path.parent_path() == parent && Path.filename().string().starts_with("phase2-")) {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }
    }
    std::filesystem::path File(const char* name) const { return Path / name; }
};
inline int Finish() {
    std::printf("%d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
} // namespace test
