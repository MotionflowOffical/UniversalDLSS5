#pragma once
#include <cstdint>
namespace udlss {
struct HostFeatureKey {
 std::uint32_t width{},height{},colorFormat{},preset{},allowUnsupported{};
 bool operator==(const HostFeatureKey&)const=default;
};
inline bool canReuseHostFeature(bool live,const HostFeatureKey& active,const HostFeatureKey& requested){
 return live && active==requested && requested.width && requested.height;
}
}
