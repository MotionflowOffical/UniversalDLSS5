include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG v1.3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

# Optional Streamline headers. Streamline's public 2.14.x package advertises
# kFeatureDLSS_NR but does not ship sl.dlss_nr.dll, so it is not the default
# DLSS-NR backend anymore.
if(UDLSS_WITH_STREAMLINE AND NOT UDLSS_STREAMLINE_ROOT)
  FetchContent_Declare(
    streamline_headers
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/Streamline.git
    GIT_TAG 2122257e0fce486f91b385aa63b9a09b0a34b363
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
  )
  FetchContent_GetProperties(streamline_headers)
  if(NOT streamline_headers_POPULATED)
    FetchContent_Populate(streamline_headers)
  endif()
  set(UDLSS_STREAMLINE_ROOT "${streamline_headers_SOURCE_DIR}" CACHE PATH "Path to Streamline SDK root" FORCE)
endif()

# The direct NR backend uses NVIDIA's public NGX SDK headers and x64 import
# library. The proprietary feature DLL nvngx_dlssnr.dll is never fetched or
# redistributed by UniversalDLSS5; the user supplies it in runtime/.
if(UDLSS_WITH_NGX_NR AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT UDLSS_NGX_ROOT)
  FetchContent_Declare(
    nvidia_dlss_sdk
    GIT_REPOSITORY https://github.com/NVIDIA/DLSS.git
    GIT_TAG 374959484e79a640feaba44c93ac8cfb0a03f5b5
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
  )
  FetchContent_GetProperties(nvidia_dlss_sdk)
  if(NOT nvidia_dlss_sdk_POPULATED)
    FetchContent_Populate(nvidia_dlss_sdk)
  endif()
  set(UDLSS_NGX_ROOT "${nvidia_dlss_sdk_SOURCE_DIR}" CACHE PATH "Path to NVIDIA/DLSS SDK root" FORCE)
endif()
