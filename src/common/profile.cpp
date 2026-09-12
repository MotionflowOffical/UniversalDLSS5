#include "udlss/profile.hpp"
#include <fstream>
#include <filesystem>
namespace udlss {
bool loadProfileFile(const std::wstring& path, Settings& out){
    std::ifstream f(std::filesystem::path(path), std::ios::binary); if(!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)),{}); return decodeProfile(data,out);
}
bool saveProfileFile(const std::wstring& path, const Settings& s){
    std::error_code ec; auto p=std::filesystem::path(path); if(p.has_parent_path()) std::filesystem::create_directories(p.parent_path(),ec);
    std::ofstream f(p,std::ios::binary|std::ios::trunc); if(!f) return false; auto d=encodeProfile(s); f.write(d.data(),static_cast<std::streamsize>(d.size())); return !!f;
}
}
