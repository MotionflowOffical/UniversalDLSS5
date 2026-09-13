#pragma once
#include <array>
#include <string_view>

namespace udlss {

enum class RuntimeImportKind : unsigned char {
    Rejected = 0,
    RequiredNgx,
    OptionalStreamline,
};

struct RuntimeImportFile {
    std::wstring_view name;
    RuntimeImportKind kind;
};

inline constexpr wchar_t runtimeAsciiLower(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
}

inline constexpr bool runtimeAsciiIEquals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (runtimeAsciiLower(a[i]) != runtimeAsciiLower(b[i])) return false;
    return true;
}

inline constexpr std::array<RuntimeImportFile, 4> runtimeImportFiles() {
    return {{{L"nvngx_dlssnr.dll", RuntimeImportKind::RequiredNgx},
             {L"sl.interposer.dll", RuntimeImportKind::OptionalStreamline},
             {L"sl.common.dll", RuntimeImportKind::OptionalStreamline},
             {L"sl.dlss_nr.dll", RuntimeImportKind::OptionalStreamline}}};
}

inline constexpr RuntimeImportKind runtimeImportKind(std::wstring_view filename) {
    for (const auto& entry : runtimeImportFiles())
        if (runtimeAsciiIEquals(filename, entry.name)) return entry.kind;
    return RuntimeImportKind::Rejected;
}

inline constexpr bool isRequiredRuntimeImport(std::wstring_view filename) {
    return runtimeImportKind(filename) == RuntimeImportKind::RequiredNgx;
}

} // namespace udlss
