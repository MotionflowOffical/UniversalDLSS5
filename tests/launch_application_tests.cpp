#include "../src/controller/launch_client.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(){
    namespace fs=std::filesystem;
    wchar_t marker[32768]{};
    if(GetEnvironmentVariableW(L"UDLSS_LAUNCH_TEST_MARKER",marker,_countof(marker))){
        // The child records its actual working directory, not the parent's inputs.
        std::ofstream out(fs::path(marker),std::ios::binary);
        out<<fs::current_path().u8string().size()<<"\n";
        const auto cwd=fs::current_path().wstring();
        out.write(reinterpret_cast<const char*>(cwd.data()),cwd.size()*sizeof(wchar_t));
        return 0;
    }
    const auto fixture=fs::temp_directory_path()/(L"UDLSS launch space \u00e1 "+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(fixture);
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,_countof(self));
    const auto executable=fixture/L"child with spaces.exe", result=fixture/L"child-result.bin";
    fs::copy_file(self,executable,fs::copy_options::overwrite_existing);
    udlss::controller::ProcessInfo child{};std::wstring message;
    bool ok=true;
    // Reject invalid paths without leaving a stale PID that could be injected.
    child.pid=123;
    if(udlss::controller::launchApplication((fixture/L"missing.exe").wstring(),child,message)||child.pid||message.empty()){
        std::cerr<<"Invalid launch left a usable process or no error\n";ok=false;
    }
    SetEnvironmentVariableW(L"UDLSS_LAUNCH_TEST_MARKER",result.c_str());
    const bool launched=udlss::controller::launchApplication(executable.wstring(),child,message);
    SetEnvironmentVariableW(L"UDLSS_LAUNCH_TEST_MARKER",nullptr);
    if(!launched||!child.pid||!child.accessible||child.arch==udlss::controller::Arch::Unknown||child.path!=executable.wstring()){
        std::wcerr<<L"Executable with spaces was not launched for early attachment: "<<message<<L"\n";ok=false;
    }
    if(launched){
        if(auto handle=OpenProcess(SYNCHRONIZE,FALSE,child.pid)){WaitForSingleObject(handle,3000);CloseHandle(handle);}
    }
    for(int i=0;launched&&i<100&&!fs::exists(result);++i)Sleep(20);
    std::ifstream in(result,std::ios::binary);std::string sizeLine;std::getline(in,sizeLine);
    const std::string bytes((std::istreambuf_iterator<char>(in)),{});
    in.close();
    const auto expected=fixture.wstring();
    if(bytes.size()!=expected.size()*sizeof(wchar_t)||memcmp(bytes.data(),expected.data(),bytes.size())){
        std::cerr<<"Child did not run beside its executable\n";ok=false;
    }
    // Copy cleanup is confined to the fixture files created above.
    for(int i=0;i<100;++i){std::error_code ec;fs::remove(executable,ec);if(!ec)break;Sleep(20);}
    fs::remove(result);fs::remove(fixture);
    return ok?0:1;
}
