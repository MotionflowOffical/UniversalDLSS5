#include "nvidia_driver_win.hpp"
#include <windows.h>
#include <vector>

namespace udlss::neural {
namespace {
bool modulePath(const wchar_t* name,std::wstring& out){
    HMODULE m=GetModuleHandleW(name);if(!m)return false;
    std::vector<wchar_t> buf(32768);DWORD n=GetModuleFileNameW(m,buf.data(),(DWORD)buf.size());
    if(!n||n>=buf.size())return false;out.assign(buf.data(),n);return true;
}
bool fileVersion(const std::wstring& path,NvidiaDriverVersion& out){
    DWORD dummy{};DWORD size=GetFileVersionInfoSizeW(path.c_str(),&dummy);if(!size)return false;
    std::vector<std::byte> data(size);if(!GetFileVersionInfoW(path.c_str(),0,size,data.data()))return false;
    VS_FIXEDFILEINFO* info{};UINT len{};if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&len)||!info||len<sizeof(VS_FIXEDFILEINFO))return false;
    out.major=HIWORD(info->dwFileVersionMS);out.minor=LOWORD(info->dwFileVersionMS);out.build=HIWORD(info->dwFileVersionLS);out.revision=LOWORD(info->dwFileVersionLS);return true;
}
}
NvidiaDriverInfo queryNvidiaDriverInfo(){
    NvidiaDriverInfo out{};std::wstring path;
    const wchar_t* modules[]={L"nvwgf2umx.dll",L"nvldumdx.dll",L"nvwgf2um.dll"};
    for(auto* m:modules)if(modulePath(m,path)&&fileVersion(path,out.version)){out.found=true;break;}
    if(out.found){wchar_t b[64]{};swprintf_s(b,L"%u.%u.%u.%u",out.version.major,out.version.minor,out.version.build,out.version.revision);out.text=b;out.directFeature18Risk=knownDirectFeature18CrashRisk(out.version);}
    return out;
}
}
