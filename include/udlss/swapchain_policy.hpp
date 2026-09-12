#pragma once
#include <cstdint>
namespace udlss {
inline bool shouldPromotePrimary(bool hasPrimary,
                                 std::uint64_t primaryLastTickMs,
                                 std::uint64_t nowTickMs,
                                 std::uint64_t currentPixels,
                                 std::uint64_t primaryPixels) {
    if(!hasPrimary) return true;
    if(nowTickMs > primaryLastTickMs && nowTickMs-primaryLastTickMs > 2000) return true;
    if(primaryPixels == 0) return currentPixels > 0;
    return currentPixels > primaryPixels + primaryPixels/2;
}
}
