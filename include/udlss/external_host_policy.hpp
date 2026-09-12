#pragma once
#include <cstdint>
#include <string>

namespace udlss {

struct NrHostSessionNames {
    std::wstring map;
    std::wstring frameEvent;
    std::wstring stopEvent;
    std::wstring doneEvent;
    std::wstring fence;
    std::wstring color;
    std::wstring output;
    std::wstring motion;
    std::wstring depth;
};

inline NrHostSessionNames makeNrHostSessionNames(std::uint32_t pid, std::uint32_t sequence) {
    const std::wstring base = L"Local\\UniversalDLSS5.NRHost." + std::to_wstring(pid) + L"." + std::to_wstring(sequence);
    return {base, base + L".Frame", base + L".Stop", base + L".Done", base + L".Fence", base + L".Color",
            base + L".Output", base + L".Motion", base + L".Depth"};
}

inline bool nrHostSequenceEnqueued(std::int32_t publishedSeq, std::int32_t requestedSeq) {
    return publishedSeq >= requestedSeq;
}

} // namespace udlss
