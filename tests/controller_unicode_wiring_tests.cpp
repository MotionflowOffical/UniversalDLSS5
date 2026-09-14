#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined
#endif

static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

static bool need(const std::string& s, const char* token, const char* message) {
    if (s.find(token) != std::string::npos) return true;
    std::cerr << message << " missing: " << token << "\n";
    return false;
}

static bool forbid(const std::string& s, const char* token, const char* message) {
    if (s.find(token) == std::string::npos) return true;
    std::cerr << message << " found: " << token << "\n";
    return false;
}

int main() {
    const std::filesystem::path root = UDLSS_SOURCE_DIR;
    const auto ui = read(root / "src/controller/ui.cpp");
    const auto cmake = read(root / "CMakeLists.txt");

    bool ok = true;
    ok &= need(ui, "L\"1\\u00D7\"", "neural-pass multiplier must use an encoding-safe Unicode escape");
    ok &= need(ui, "L\"1\\u00D7 full\"", "flow-resolution multiplier must use an encoding-safe Unicode escape");
    ok &= need(ui, "L\" \\u2192 \"", "status arrow must use an encoding-safe Unicode escape");
    ok &= need(ui, "L\" \\u00B7 \"", "status separator must use an encoding-safe Unicode escape");
    ok &= need(ui, "L\"Passes 2\\u20134", "en dash must use an encoding-safe Unicode escape");
    ok &= need(cmake, "/utf-8", "MSVC source encoding must be explicitly UTF-8");

    ok &= forbid(ui, "Ã", "controller source contains mojibake");
    for (unsigned char c : ui) {
        if (c >= 0x80) {
            std::cerr << "controller UI source must keep visible Unicode as ASCII-safe escapes\n";
            ok = false;
            break;
        }
    }
    return ok ? 0 : 1;
}
