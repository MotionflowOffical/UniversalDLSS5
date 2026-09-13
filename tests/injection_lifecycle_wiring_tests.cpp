#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined by CMake
#endif

static std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        std::cerr << "Could not open " << p << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::filesystem::path root = UDLSS_SOURCE_DIR;
    const auto injector = readFile(root / "src" / "injector" / "injector.cpp");
    const auto bridge = readFile(root / "src" / "bridge" / "bridge_main.cpp");
    const auto client = readFile(root / "src" / "controller" / "inject_client.cpp");

    if (injector.find("WaitForMultipleObjects") == std::string::npos ||
        injector.find("resolveRemoteProcAddress") == std::string::npos ||
        injector.find("UniversalDLSS5_BridgeStart") == std::string::npos ||
        injector.find("inject-") == std::string::npos ||
        injector.find("WaitForSingleObject(t,10000)") != std::string::npos) {
        std::cerr << "Injector still has the unsafe LoadLibrary timeout/address/startup lifecycle\n";
        return 1;
    }

    const auto dllMainPos = bridge.find("BOOL WINAPI DllMain");
    if (bridge.find("UniversalDLSS5_BridgeStart") == std::string::npos ||
        dllMainPos == std::string::npos ||
        bridge.substr(dllMainPos).find("CreateThread") != std::string::npos) {
        std::cerr << "Bridge startup still creates its worker from DllMain/loader lock\n";
        return 1;
    }

    if (client.find("WAIT_TIMEOUT") == std::string::npos ||
        client.find("injection pending") == std::string::npos ||
        client.find("12000") != std::string::npos) {
        std::cerr << "Controller does not preserve pending background injections\n";
        return 1;
    }

    return 0;
}
