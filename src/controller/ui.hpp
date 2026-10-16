#pragma once
#include <windows.h>
#include <string>
namespace udlss::controller { int runUi(HINSTANCE instance,int show,const std::wstring& launchExecutable={}); }
