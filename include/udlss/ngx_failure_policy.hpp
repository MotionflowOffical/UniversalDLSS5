#pragma once
#include <cstdint>
#include <cwchar>
#include <sstream>
#include <string>

namespace udlss {

enum class NgxFailureStage : std::uint32_t { None=0, Initialize, CreateFeature, EvaluateFeature };

struct NgxFailureState {
    bool valid{};
    NgxFailureStage stage{NgxFailureStage::None};
    std::int32_t result{};
    std::uint32_t exceptionCode{};
};

inline void clearNgxFailure(NgxFailureState& s) { s={}; }
inline void keepExistingNgxFailure(NgxFailureState&) {}
inline void rememberNgxFailure(NgxFailureState& s,NgxFailureStage stage,std::int32_t result,std::uint32_t exceptionCode) {
    s.valid=true; s.stage=stage; s.result=result; s.exceptionCode=exceptionCode;
}

inline const wchar_t* ngxFailureStageName(NgxFailureStage stage) {
    switch(stage) {
    case NgxFailureStage::Initialize: return L"Initialize";
    case NgxFailureStage::CreateFeature: return L"CreateFeature(18)";
    case NgxFailureStage::EvaluateFeature: return L"EvaluateFeature(18)";
    default: return L"NGX";
    }
}

inline const wchar_t* ngxResultName(std::uint32_t result) {
    switch(result) {
    case 0xBAD00000u: return L"Fail";
    case 0xBAD00001u: return L"FeatureNotSupported";
    case 0xBAD00002u: return L"PlatformError";
    case 0xBAD00003u: return L"FeatureAlreadyExists";
    case 0xBAD00004u: return L"FeatureNotFound";
    case 0xBAD00005u: return L"InvalidParameter";
    case 0xBAD00006u: return L"ScratchBufferTooSmall";
    case 0xBAD00007u: return L"NotInitialized";
    case 0xBAD00008u: return L"UnsupportedInputFormat";
    case 0xBAD00009u: return L"RWFlagMissing";
    case 0xBAD0000Au: return L"MissingInput";
    case 0xBAD0000Bu: return L"UnableToInitializeFeature";
    case 0xBAD0000Cu: return L"OutOfDate";
    case 0xBAD0000Du: return L"OutOfGPUMemory";
    case 0xBAD0000Eu: return L"UnsupportedFormat";
    case 0xBAD0000Fu: return L"UnableToWriteToAppDataPath";
    case 0xBAD00010u: return L"UnsupportedParameter";
    case 0xBAD00011u: return L"Denied";
    case 0xBAD00012u: return L"NotImplemented";
    default: return L"UnknownResult";
    }
}

inline std::wstring formatNgxFailure(const NgxFailureState& s) {
    if(!s.valid) return {};
    std::wstringstream out;
    out << ngxFailureStageName(s.stage) << L": ";
    if(s.exceptionCode) {
        wchar_t hex[16]{};
        swprintf(hex,16,L"%08X",static_cast<unsigned>(s.exceptionCode));
        out << L"exception 0x" << hex;
    } else {
        const auto u=static_cast<std::uint32_t>(s.result);
        wchar_t hex[16]{};
        swprintf(hex,16,L"%08X",static_cast<unsigned>(u));
        out << L"0x" << hex << L" (" << ngxResultName(u) << L")";
    }
    return out.str();
}

} // namespace udlss
