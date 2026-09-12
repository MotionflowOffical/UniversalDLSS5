#include "udlss/ngx_failure_policy.hpp"
#include <cassert>
#include <string>

using namespace udlss;

int main() {
    NgxFailureState state{};
    rememberNgxFailure(state, NgxFailureStage::CreateFeature, static_cast<std::int32_t>(0xBAD00001u), 0);
    assert(state.valid);
    assert(state.stage == NgxFailureStage::CreateFeature);
    assert(static_cast<std::uint32_t>(state.result) == 0xBAD00001u);

    const auto first = formatNgxFailure(state);
    assert(first.find(L"CreateFeature(18)") != std::wstring::npos);
    assert(first.find(L"0xBAD00001") != std::wstring::npos);
    assert(first.find(L"FeatureNotSupported") != std::wstring::npos);

    // Once a session failure is recorded, a later generic frame must not erase it.
    const auto before = state;
    keepExistingNgxFailure(state);
    assert(state.valid == before.valid && state.result == before.result && state.stage == before.stage);

    clearNgxFailure(state);
    assert(!state.valid);

    rememberNgxFailure(state, NgxFailureStage::EvaluateFeature, 0, 0xC0000005u);
    const auto exceptionText = formatNgxFailure(state);
    assert(exceptionText.find(L"EvaluateFeature(18)") != std::wstring::npos);
    assert(exceptionText.find(L"exception 0xC0000005") != std::wstring::npos);
    return 0;
}
