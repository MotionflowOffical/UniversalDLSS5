#pragma once
#include <windows.h>
#include "udlss/shared_control.hpp"
namespace udlss::bridge { bool installOpenGLHooks(HMODULE self,SharedControl* control); void shutdownOpenGLCompatibility(); }
