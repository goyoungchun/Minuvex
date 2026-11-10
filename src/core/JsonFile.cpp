#include "JsonFile.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace lv::detail {
using Json=nlohmann::json;
Json ReadJsonFile(const std::filesystem::path& path,std::size_t limit) {
    if (path.empty()) {throw std::invalid_argument("Archive path is empty.");}
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if (!stream) {throw std::runtime_error("Cannot open archive file.");}
    const auto size=stream.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(limit)) {throw std::invalid_argument("Archive JSON is empty or exceeds its byte limit.");}
    std::string bytes(static_cast<std::size_t>(size),'\0');stream.seekg(0);
    if (!stream.read(bytes.data(),size)) {throw std::runtime_error("Cannot read complete archive file.");}
    std::unordered_map<int,std::unordered_set<std::string>> keys;
    return Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value) {
        if (depth > 32) {throw std::invalid_argument("Archive JSON nesting limit exceeded.");}
        if (event == Json::parse_event_t::object_start) {keys[depth].clear();}
        if (event == Json::parse_event_t::key && !keys[depth-1].insert(value.get<std::string>()).second) {throw std::invalid_argument("Duplicate JSON key.");}
        return true;
    });
}
void WriteJsonFile(const Json& json,const std::filesystem::path& path,std::size_t limit) {
    WriteTextFile(json.dump(2)+'\n',path,limit);
}
void WriteTextFile(std::string_view bytes,const std::filesystem::path& path,std::size_t limit) {
    if (path.empty()) {throw std::invalid_argument("Archive path is empty.");}
    if (bytes.size() > limit) {throw std::invalid_argument("Archive exceeds its byte limit.");}
    const auto destination=std::filesystem::absolute(path).lexically_normal();
    static std::atomic_uint64_t sequence{0};auto temporary=destination;
    temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
    try {
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
        if (!stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()))) {throw std::runtime_error("Cannot write temporary archive file.");}
        stream.close();if (!stream) {throw std::runtime_error("Cannot close temporary archive file.");}
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {throw std::runtime_error("Cannot replace archive file (Windows error "+std::to_string(GetLastError())+").");}
#else
        std::filesystem::rename(temporary,destination);
#endif
    } catch (...) {std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
}
