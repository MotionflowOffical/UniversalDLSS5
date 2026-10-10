#pragma once

namespace udlss {

// Mutating a backbuffer before Present is only safe when Present is expected to
// consume it. TEST never consumes the image, and DO_NOT_WAIT may fail without
// advancing the swapchain. After a non-consuming/occluded result, allow one
// clean unmodified Present to re-establish swapchain progress before processing
// resumes.
inline constexpr bool shouldProcessPresentBeforeMutation(bool testPresent,
                                                         bool doNotWait,
                                                         bool recoverySuppressed) {
    return !testPresent && !doNotWait && !recoverySuppressed;
}

inline constexpr bool shouldClearPresentSuppression(long presentResult) {
    return presentResult==0; // S_OK
}

inline constexpr bool shouldArmPresentSuppression(long presentResult) {
    return presentResult!=0;
}

inline constexpr bool shouldCountSuccessfulPresent(bool testPresent,long presentResult) {
    return !testPresent && presentResult==0; // S_OK only; status/occluded calls are not displayed frames
}

} // namespace udlss
