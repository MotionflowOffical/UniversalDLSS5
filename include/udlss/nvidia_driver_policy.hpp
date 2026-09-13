#pragma once
#include <cstdint>

namespace udlss {
struct NvidiaDriverVersion {
    std::uint16_t major{}, minor{}, build{}, revision{};
};

inline bool knownDirectFeature18CrashRisk(const NvidiaDriverVersion& v) {
    return v.major == 32 && v.minor == 0 && v.build == 16 &&
           (v.revision == 1664 || v.revision == 1686);
}
} // namespace udlss
