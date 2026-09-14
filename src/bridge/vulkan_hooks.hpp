#pragma once
#include "udlss/shared_control.hpp"
#include <windows.h>

namespace udlss::bridge {
bool installVulkanHooks(HMODULE self,SharedControl* control);
void shutdownVulkanCompatibility();
}
