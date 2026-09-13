#pragma once
#include "udlss/nvidia_driver_policy.hpp"
#include <string>
namespace udlss::neural {
struct NvidiaDriverInfo {
    NvidiaDriverVersion version{};
    std::wstring text;
    bool found{};
    bool directFeature18Risk{};
};
NvidiaDriverInfo queryNvidiaDriverInfo();
}
