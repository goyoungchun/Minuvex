#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace lv {

enum class ErrorCode {
    AssetLoad, InvalidData, UnsupportedFeature, InvalidArgument, InvalidHandle,
    InvalidState, WrongThread, GraphicsInitialization, Rendering,
    InsufficientResource, CallbackFailure, SceneIo, QualityIo, TelemetryIo
};

struct Error {
    ErrorCode Code = ErrorCode::InvalidState;
    std::string Message;
    std::string Hint;
    std::filesystem::path SourcePath;
};

// Accessing the wrong alternative is a programming error (std::logic_error).
// Recoverable operation failures are returned as Error, not thrown by Value().
template<typename T>
class [[nodiscard]] Result {
public:
    static Result Success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result Failure(lv::Error error) { return Result(std::in_place_index<1>, std::move(error)); }
    bool HasValue() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return HasValue(); }

    T& Value() & { RequireValue(); return std::get<0>(data_); }
    const T& Value() const& { RequireValue(); return std::get<0>(data_); }
    T&& Value() && { RequireValue(); return std::move(std::get<0>(data_)); }
    const lv::Error& Error() const& {
        if (HasValue()) { throw std::logic_error("Successful Result has no Error."); }
        return std::get<1>(data_);
    }
    lv::Error Error() && {
        if (HasValue()) { throw std::logic_error("Successful Result has no Error."); }
        return std::move(std::get<1>(data_));
    }

private:
    template<std::size_t I, typename U>
    Result(std::in_place_index_t<I> index, U&& value) : data_(index, std::forward<U>(value)) {}
    void RequireValue() const { if (!HasValue()) { throw std::logic_error("Failed Result has no Value; inspect Error()."); } }
    std::variant<T, lv::Error> data_;
};

template<>
class [[nodiscard]] Result<void> {
public:
    static Result Success() { return Result(std::monostate{}); }
    static Result Failure(lv::Error error) { return Result(std::move(error)); }
    bool HasValue() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return HasValue(); }
    void Value() const { if (!HasValue()) { throw std::logic_error("Failed Result has no Value; inspect Error()."); } }
    const lv::Error& Error() const& {
        if (HasValue()) { throw std::logic_error("Successful Result has no Error."); }
        return std::get<1>(data_);
    }
    lv::Error Error() && {
        if (HasValue()) { throw std::logic_error("Successful Result has no Error."); }
        return std::move(std::get<1>(data_));
    }

private:
    explicit Result(std::monostate value) : data_(value) {}
    explicit Result(lv::Error error) : data_(std::move(error)) {}
    std::variant<std::monostate, lv::Error> data_;
};

} // namespace lv
