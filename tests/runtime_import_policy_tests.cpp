#include "udlss/runtime_import_policy.hpp"
#include <iostream>

using namespace udlss;

int main() {
    if (runtimeImportKind(L"nvngx_dlssnr.dll") != RuntimeImportKind::RequiredNgx) return 1;
    if (runtimeImportKind(L"NVNGX_DLSSNR.DLL") != RuntimeImportKind::RequiredNgx) return 2;
    if (runtimeImportKind(L"sl.interposer.dll") != RuntimeImportKind::OptionalStreamline) return 3;
    if (runtimeImportKind(L"SL.COMMON.DLL") != RuntimeImportKind::OptionalStreamline) return 4;
    if (runtimeImportKind(L"sl.dlss_nr.dll") != RuntimeImportKind::OptionalStreamline) return 5;
    if (runtimeImportKind(L"UniversalDLSS5.Bridge.dll") != RuntimeImportKind::Rejected) return 6;
    if (runtimeImportKind(L"nvngx.dll_UniversalDLSS5_NRForwarder.dll") != RuntimeImportKind::Rejected) return 7;
    if (runtimeImportKind(L"nvngx_dlss.dll") != RuntimeImportKind::Rejected) return 8;
    if (runtimeImportKind(L"random.dll") != RuntimeImportKind::Rejected) return 9;
    if (runtimeImportFiles().size() != 4) return 10;
    return 0;
}
