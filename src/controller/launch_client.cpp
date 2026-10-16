#include "launch_client.hpp"
#include "udlss/injection_policy.hpp"
#include <filesystem>

namespace udlss::controller {
bool launchApplication(const std::wstring& executable, ProcessInfo& process, std::wstring& message) {
    process={};
    std::error_code ec;
    const auto path=std::filesystem::absolute(executable,ec);
    if(ec||!std::filesystem::is_regular_file(path,ec)){
        message=L"Application executable was not found";return false;
    }
    // Supply both the executable and a quoted argv[0]. The executable's own
    // directory is the working directory, independent of the controller's path.
    auto command=L"\""+path.wstring()+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,path.parent_path().c_str(),&si,&pi)){
        message=L"Could not launch application (Windows error "+std::to_wstring(GetLastError())+L")";return false;
    }
    process.pid=pi.dwProcessId;process.parentPid=GetCurrentProcessId();
    process.path=path.wstring();process.name=path.filename().wstring();
    process.arch=processArch(pi.hProcess);process.accessible=true;
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature{};
    if(GetProcessMitigationPolicy(pi.hProcess,ProcessSignaturePolicy,&signature,sizeof(signature)))
        process.blocksThirdPartyModules=blocksThirdPartyModules({signature.MicrosoftSignedOnly!=0,signature.StoreSignedOnly!=0});
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    message=L"Application launched";return true;
}
}
