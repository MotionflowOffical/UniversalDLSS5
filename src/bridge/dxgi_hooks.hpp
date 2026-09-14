#pragma once
#include <windows.h>
#include <dxgi1_4.h>
#include "udlss/shared_control.hpp"
namespace udlss::bridge { bool installHooks(HMODULE self,SharedControl* control); DetachMode removeHooks(); }
