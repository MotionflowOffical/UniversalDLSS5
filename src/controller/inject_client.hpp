#pragma once
#include "processes.hpp"
#include <string>
namespace udlss::controller { bool injectBridge(const ProcessInfo&,const std::wstring& appDir,std::wstring& message); }
