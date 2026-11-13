#pragma once
#include <cstdint>
#include <minvex/Scene.h>
namespace lv::detail {
inline void HashResourceIdentity(std::uint64_t& hash,const void* identity) noexcept {
    const auto value=reinterpret_cast<std::uintptr_t>(identity);
    for (int i=0;i<8;++i) {hash^=(static_cast<std::uint64_t>(value)>>(i*8))&255;hash*=1099511628211ull;}
}
inline void HashMaterialResources(std::uint64_t& hash,const Scene& scene) {
    const auto integer=[&](std::uint64_t value) {for (int i=0;i<8;++i) {hash^=(value>>(i*8))&255;hash*=1099511628211ull;}};
    for (const auto& item:scene.CollectRenderItems()) {if (!item.MaterialOverrides || item.MaterialOverrides->empty()) {continue;}integer(item.Object);
        for (std::size_t index=0;index<item.Model->Materials.size();++index) {const auto found=item.MaterialOverrides->find(index);if (found == item.MaterialOverrides->end()) {continue;}const auto& material=found->second;integer(index);integer(static_cast<unsigned>(material.Shading));integer(material.Unlit);for (auto texture:{material.BaseColorTexture,material.NormalTexture,material.MetallicRoughnessTexture}) {integer(texture.has_value());integer(texture.value_or(0));}}
    }
}
}
