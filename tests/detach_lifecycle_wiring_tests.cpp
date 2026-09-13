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
    if (!f) { std::cerr << "Could not open " << p << "\n"; std::exit(2); }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::filesystem::path root = UDLSS_SOURCE_DIR;
    const auto dxgi = readFile(root / "src" / "bridge" / "dxgi_hooks.cpp");
    const auto bridge = readFile(root / "src" / "bridge" / "bridge_main.cpp");
    const auto d11res = readFile(root / "src" / "gpu" / "d3d11_resource_tracker.cpp");
    const auto d11cam = readFile(root / "src" / "gpu" / "d3d11_camera_tracker.cpp");
    const auto d12res = readFile(root / "src" / "gpu" / "d3d12_resource_tracker.cpp");
    const auto guides = readFile(root / "src" / "gpu" / "game_temporal_guides.cpp");

    const auto begin = dxgi.find("beginHookUnload()");
    const auto disable = dxgi.find("MH_DisableHook(MH_ALL_HOOKS)", begin);
    const auto drain = dxgi.find("waitForHookIdle", disable);
    const auto retire = dxgi.find("retired.swap(swaps)", drain);
    const auto uninit = dxgi.find("MH_Uninitialize()", retire);
    if (begin == std::string::npos || disable == std::string::npos ||
        drain == std::string::npos || retire == std::string::npos ||
        uninit == std::string::npos || !(begin < disable && disable < drain && drain < retire && retire < uninit)) {
        std::cerr << "Detach does not quiesce in-flight hooks before destroying pipeline state/MinHook\n";
        return 1;
    }

    if (bridge.find("removeHooks();") == std::string::npos ||
        bridge.find("FreeLibraryAndExitThread") == std::string::npos ||
        bridge.find("removeHooks();") > bridge.rfind("FreeLibraryAndExitThread")) {
        std::cerr << "Bridge can unload before hook teardown completes\n";
        return 1;
    }

    for (const auto* source : {&dxgi, &d11res, &d11cam, &d12res, &guides}) {
        if (source->find("HookCallScope") == std::string::npos) {
            std::cerr << "A hooked graphics subsystem is missing detach lifetime accounting\n";
            return 1;
        }
    }

    // Present must bypass all injector work once unloading starts while still
    // forwarding the game's original Present.
    if (dxgi.find("if(!call.customWorkAllowed())return origPresent") == std::string::npos ||
        dxgi.find("if(!call.customWorkAllowed())return origPresent1") == std::string::npos) {
        std::cerr << "Present does not switch to direct game passthrough during detach\n";
        return 1;
    }

    return 0;
}
