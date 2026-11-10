#pragma once
#include <cstdint>
namespace lv::detail {
inline void HashResourceIdentity(std::uint64_t& hash,const void* identity) noexcept {
    const auto value=reinterpret_cast<std::uintptr_t>(identity);
    for (int i=0;i<8;++i) {hash^=(static_cast<std::uint64_t>(value)>>(i*8))&255;hash*=1099511628211ull;}
}
}
