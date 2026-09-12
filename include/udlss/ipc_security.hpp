#pragma once
#include <string>
#include <string_view>
namespace udlss {
inline std::wstring lowIntegrityMappingSddl(std::wstring_view userSid) {
    std::wstring out=L"D:(A;;GA;;;";
    out.append(userSid);
    out += L")S:(ML;;NW;;;LW)";
    return out;
}
} // namespace udlss
