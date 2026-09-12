#pragma once
namespace udlss {
struct ProcessModulePolicy {
    bool microsoftSignedOnly{};
    bool storeSignedOnly{};
};
inline constexpr bool blocksThirdPartyModules(ProcessModulePolicy p) {
    return p.microsoftSignedOnly || p.storeSignedOnly;
}
} // namespace udlss
