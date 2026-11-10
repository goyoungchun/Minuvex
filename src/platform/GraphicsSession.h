#pragma once
#include <atomic>
namespace lv::detail {
inline std::atomic<void*> GraphicsSessionOwner{nullptr};
class GraphicsLease {
public:
    explicit GraphicsLease(void* owner):owner_(owner) {}
    GraphicsLease(const GraphicsLease&)=delete;
    GraphicsLease& operator=(const GraphicsLease&)=delete;
    ~GraphicsLease() {Release();}
    bool Acquire() noexcept {void* expected=nullptr;held_=GraphicsSessionOwner.compare_exchange_strong(expected,owner_);return held_;}
    void Release() noexcept {if (held_) {void* expected=owner_;GraphicsSessionOwner.compare_exchange_strong(expected,nullptr);held_=false;}}
private:
    void* owner_;
    bool held_=false;
};
}
