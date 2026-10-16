#pragma once
#include "processes.hpp"

namespace udlss::controller {
bool launchApplication(const std::wstring& executable, ProcessInfo& process, std::wstring& message);
}
