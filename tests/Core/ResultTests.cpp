#include <minvex/Result.h>

#include <cstdio>
#include <memory>
#include <type_traits>

namespace {
int checks = 0, failures = 0;
void Check(bool value, const char* message) {
    ++checks; if (!value) { ++failures; std::fprintf(stderr,"FAIL: %s\n",message); }
}
template<typename Function>
void LogicError(Function function, const char* message) {
    try { function(); Check(false,message); }
    catch (const std::logic_error&) { Check(true,message); }
}
}

int main() {
    const auto success = lv::Result<int>::Success(42);
    Check(success.HasValue() && bool(success) && success.Value() == 42,"value result exposes success");
    static_assert(std::is_same_v<decltype(success.Value()),const int&>);
    auto mutableValue = lv::Result<int>::Success(2);
    mutableValue.Value() = 3;
    Check(mutableValue.Value() == 3,"non-const value can be edited");
    const lv::Error error{lv::ErrorCode::AssetLoad,"missing asset","Correct the path.","model.glb"};
    const auto failure = lv::Result<int>::Failure(error);
    Check(!failure && !failure.HasValue(),"failed value result is false");
    Check(failure.Error().Code == lv::ErrorCode::AssetLoad && failure.Error().SourcePath == "model.glb","failure retains structured context");
    Check(failure.Error().Hint == error.Hint && failure.Error().Message == error.Message,"failure retains diagnostic and recovery hint");
    const auto copy = failure;
    Check(copy.Error().SourcePath == failure.Error().SourcePath,"copyable results retain the error");
    LogicError([&] { (void)failure.Value(); },"wrong value access is a programming error");
    LogicError([&] { (void)success.Error(); },"successful value has no error");
    auto moveOnly = lv::Result<std::unique_ptr<int>>::Success(std::make_unique<int>(7));
    auto moved = std::move(moveOnly).Value();
    Check(moved && *moved == 7,"results support move-only values");
    auto moveFailure = lv::Result<std::unique_ptr<int>>::Failure(error);
    Check(!moveFailure,"move-only result may fail without constructing a value");
    const auto extracted = lv::Result<int>::Failure(error).Error();
    Check(extracted.Message == error.Message,"temporary errors are extracted by value");
    const auto ok = lv::Result<void>::Success();
    Check(ok.HasValue() && bool(ok),"void success is supported");
    ok.Value();
    LogicError([&] { (void)ok.Error(); },"void success has no error");
    const auto failedVoid = lv::Result<void>::Failure(error);
    Check(!failedVoid && failedVoid.Error().Code == error.Code,"void failure retains error");
    LogicError([&] { failedVoid.Value(); },"void error cannot be accessed as success");
    Check(lv::Result<void>::Failure(error).Error().Hint == error.Hint,"void temporary error is safely copied/moved out");
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
