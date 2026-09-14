#pragma once
#include <windows.h>
#include "udlss/shared_control.hpp"
namespace udlss::bridge {
bool installD3D9Hooks(HMODULE self,SharedControl* control);
bool ensureD3D9HooksInstalled(HMODULE self,SharedControl* control);
void shutdownD3D9Compatibility();
}
