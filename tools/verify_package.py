#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
ALLOW_LOCAL_RUNTIME = "--allow-local-runtime" in sys.argv[1:]
REQUIRED = [
    'CMakeLists.txt', 'BUILD_WINDOWS.bat', 'README.md', 'THIRD_PARTY.md',
    'include/udlss/settings.hpp', 'include/udlss/shared_control.hpp', 'include/udlss/guide_quality_policy.hpp', 'include/udlss/resource_extraction_policy.hpp', 'include/udlss/game_guides_api.hpp',
    'include/udlss/native_motion_policy.hpp', 'include/udlss/camera_matrix_policy.hpp', 'include/udlss/camera_motion_math.hpp', 'include/udlss/motion_route_policy.hpp',
    'include/udlss/runtime_policy.hpp', 'include/udlss/injection_policy.hpp',
    'include/udlss/runtime_diagnostics.hpp', 'include/udlss/d3d12_backbuffer_policy.hpp', 'include/udlss/backend_policy.hpp', 'include/udlss/external_host_policy.hpp',
    'src/controller/ui.cpp', 'src/bridge/dxgi_hooks.cpp',
    'src/gpu/d3d11_pipeline.cpp', 'src/gpu/d3d11_guide_extractor.cpp', 'src/gpu/d3d11_resource_tracker.cpp', 'src/gpu/d3d11_resource_tracker.hpp', 'src/gpu/d3d11_camera_tracker.cpp', 'src/gpu/d3d11_camera_tracker.hpp', 'src/gpu/d3d12_on12.cpp', 'src/gpu/nv_optical_flow.cpp',
    'src/neural/ingame_nr.cpp', 'src/neural/ngx_nr.cpp', 'src/neural/external_host.cpp', 'src/neural/external_host_protocol.hpp', 'src/neural/streamline_nr.cpp', 'src/neural/passthrough.cpp', 'src/host/main.cpp', 'src/host/nr_forwarder.cpp',
    'shaders/convert.hlsl', 'shaders/downsample.hlsl', 'shaders/flow.hlsl',
    'shaders/motion.hlsl', 'shaders/native_motion_convert.hlsl', 'shaders/camera_motion.hlsl', 'shaders/mask.hlsl', 'shaders/depth_convert.hlsl', 'shaders/post.hlsl', 'shaders/blit.hlsl',
    'runtime/README.txt',
    'tests/guide_quality_policy_tests.cpp', 'tests/ingame_nr_policy_tests.cpp', 'tests/ingame_nr_parameters_tests.cpp', 'tests/streamline_mount_policy_tests.cpp', 'tests/motion_route_policy_tests.cpp', 'tests/camera_motion_math_tests.cpp', 'tests/d3d11_direct_mount_motion_tests.cpp', 'tests/d3d11_depth_tracking_tests.cpp', 'tests/camera_matrix_policy_tests.cpp', 'tests/native_motion_policy_tests.cpp', 'tests/resource_tracker_semantic_tests.cpp', 'tests/resource_extraction_policy_tests.cpp', 'tests/runtime_diagnostics_tests.cpp', 'tests/d3d12_backbuffer_policy_tests.cpp', 'tests/backend_policy_tests.cpp',
    'tests/windows_build_wiring_tests.cpp', 'tests/app_picker_policy_tests.cpp', 'tests/ngx_failure_policy_tests.cpp', 'tests/neural_route_policy_tests.cpp', 'tests/external_host_policy_tests.cpp',
    'include/udlss/app_picker_policy.hpp', 'include/udlss/ngx_failure_policy.hpp', 'include/udlss/neural_route_policy.hpp',
    'examples/GameGuidesAdapter/README.md', 'examples/GameGuidesAdapter/template.cpp',
]
PROPRIETARY_NAMES = {
    'nvngx_dlssnr.dll', 'nvngx_dlss.dll', 'sl.interposer.dll', 'sl.common.dll', 'sl.dlss_nr.dll',
    'nvsdk_ngx_d.lib', 'nvsdk_ngx_s.lib'
}
SKIP_DIRS = {'build', 'build-linux', '.git', '__pycache__'}

def fail(msg: str) -> None:
    print(f'FAIL: {msg}', file=sys.stderr)
    raise SystemExit(1)

for rel in REQUIRED:
    if not (ROOT / rel).is_file():
        fail(f'missing required source file: {rel}')

# Ensure proprietary runtime binaries are never accidentally packed with source.
for p in ROOT.rglob('*'):
    if any(part in SKIP_DIRS or part.startswith('build-') for part in p.parts):
        continue
    if p.is_file() and p.name.lower() in PROPRIETARY_NAMES:
        rel = p.relative_to(ROOT)
        if ALLOW_LOCAL_RUNTIME and len(rel.parts) >= 2 and rel.parts[0].lower() == 'runtime':
            continue
        fail(f'proprietary runtime binary must not be shipped: {rel}')

cmake = (ROOT / 'cmake/Dependencies.cmake').read_text(encoding='utf-8')
if '2122257e0fce486f91b385aa63b9a09b0a34b363' not in cmake:
    fail('optional Streamline header dependency is not pinned to the audited 2.14.1 commit')
if '374959484e79a640feaba44c93ac8cfb0a03f5b5' not in cmake:
    fail('NVIDIA/DLSS NGX SDK dependency is not pinned')

runtime_policy = (ROOT / 'include/udlss/runtime_policy.hpp').read_text(encoding='utf-8')
required_body = runtime_policy.split('requiredDlssNrRuntimeFiles()',1)[1].split('}',1)[0]
if 'nvngx_dlssnr.dll' not in required_body or 'sl.dlss_nr.dll' in required_body:
    fail('runtime validator must require nvngx_dlssnr.dll without requiring sl.dlss_nr.dll')

ngx_backend = (ROOT / 'src/neural/ngx_nr.cpp').read_text(encoding='utf-8')
for forbidden in ['NVSDK_NGX_D3D11_Init_with_ProjectID', 'NVSDK_NGX_D3D11_CreateFeature',
                  'NVSDK_NGX_D3D11_EvaluateFeature']:
    if forbidden in ngx_backend:
        fail(f'DLSS-NR backend regressed to the D3D11 NGX route: {forbidden}')
for token in ['NVSDK_NGX_Feature_Reserved18', 'NVSDK_NGX_D3D12_Init_with_ProjectID',
              'NVSDK_NGX_D3D12_AllocateParameters(&params_)', 'LoadLibraryExW', 'GetProcAddress',
              '"NVSDK_NGX_D3D12_Init_Ext"', '"NVSDK_NGX_D3D12_PopulateParameters_Impl"',
              'snippetPopulate_', 'snippetCreate_', 'snippetEvaluate_',
              'D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE', 'OpenSharedHandle', 'OpenSharedFence']:
    if token not in ngx_backend:
        fail(f'D3D12-unified NGX backend missing expected API usage: {token}')

if 'safeSnippetInit(snippetInit_,kFallbackSnippetApplicationId,runtime_.c_str(),d12_.Get(),nullptr,initException)' not in ngx_backend:
    fail('DLSS-NR signed snippet init does not match the verified app-id/runtime-path/null-params contract')
for token in ['NVSDK_NGX_GetApplicationId', 'snippetApplicationId_',
              'DLSSNRComputeScalingRatioCallback', 'NVSDK_NGX_Parameter_PerfQualityValue']:
    if token not in ngx_backend:
        fail(f'DLSS-NR compatibility contract missing: {token}')
if 'D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX' in ngx_backend:
    fail('Direct NGX shared textures must use SHARED|SHARED_NTHANDLE, not keyed mutex synchronization')
for forbidden in ['NVSDK_NGX_Parameter_SetF(params_,kPaperWhite',
                  'NVSDK_NGX_Parameter_SetF(params_,kTransferStrength',
                  'NVSDK_NGX_Parameter_SetF(params_,kColorStrength']:
    if forbidden in ngx_backend:
        fail(f'Direct Feature-18 path sends a non-contract generic parameter: {forbidden}')
for token in ['DLSS.Indicator.Invert.X.Axis', 'DLSS.Indicator.Invert.Y.Axis']:
    if token not in ngx_backend:
        fail(f'Direct Feature-18 evaluate contract missing: {token}')
if 'NVSDK_NGX_D3D11_CreateFeature' in ngx_backend or 'NVSDK_NGX_D3D11_EvaluateFeature' in ngx_backend:
    fail('DLSS-NR execution must remain D3D12-only')


external_backend = (ROOT / 'src/neural/external_host.cpp').read_text(encoding='utf-8')
host_source = (ROOT / 'src/host/main.cpp').read_text(encoding='utf-8')
host_protocol = (ROOT / 'src/neural/external_host_protocol.hpp').read_text(encoding='utf-8')
cmake_root = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
for token in ['D3D11_RESOURCE_MISC_SHARED_NTHANDLE', 'D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX',
              'CreateSharedHandle', 'UniversalDLSS5.NRHost.exe', 'WaitForMultipleObjects',
              'enqueuedSeq', 'doneEvent_']:
    if token not in external_backend:
        fail(f'external-host bridge transport missing: {token}')
for token in ['OpenSharedHandle(', 'NVSDK_NGX_D3D12_CreateFeature', 'CoreDispatch',
              'SignedSnippet', 'UdlssNrFwdInitExt', 'UdlssNrFwdCreate', 'UdlssNrFwdEvaluate',
              'MH_CreateHook', 'NVAPI_ID_GET_ARCH', 'DLSSNRComputeScalingRatioCallback',
              'NVSDK_NGX_Parameter_PerfQualityValue', 'enqueuedSeq']:
    if token not in host_source:
        fail(f'external NR host implementation missing: {token}')
forwarder_source = (ROOT / 'src/host/nr_forwarder.cpp').read_text(encoding='utf-8')
for token in ['UdlssNrFwdLoad', 'UdlssNrFwdInitExt', 'UdlssNrFwdPopulate', 'UdlssNrFwdCreate',
              'UdlssNrFwdEvaluate', 'UdlssNrFwdRelease', 'UdlssNrFwdShutdown',
              '__declspec(noinline)', 'InterlockedExchange']:
    if token not in forwarder_source:
        fail(f'NR caller-compatible forwarder missing: {token}')
if 'nvngx.dll_UniversalDLSS5_NRForwarder' not in cmake_root:
    fail('CMake forwarder output name must carry the nvngx.dll compatibility marker')
if 'DuplicateHandle' not in external_backend:
    fail('external-host bridge transport must duplicate GPU handles into NRHost')
if 'OpenSharedHandleByName' in host_source:
    fail('external NR host must not rediscover D3D11-created GPU objects by name')
for token in ['kAbi=4', 'fenceHandle', 'colorHandle', 'outputHandle', 'motionHandle', 'depthHandle', 'allowUnsupportedHardware', 'realGpuArchitecture', 'reportedGpuArchitecture', 'architectureCompatibilityActive']:
    if token not in host_protocol:
        fail(f'external-host protocol missing duplicated-handle field: {token}')
for token in ['kMagic', 'kAbi', 'inputFenceValue', 'outputFenceValue', 'enqueuedSeq']:
    if token not in host_protocol:
        fail(f'external-host protocol missing field: {token}')
if 'add_executable(UniversalDLSS5.NRHost WIN32' not in cmake_root or 'src/host/main.cpp' not in cmake_root:
    fail('CMake does not build the x64 UniversalDLSS5.NRHost executable')
settings_source = (ROOT / 'include/udlss/settings.hpp').read_text(encoding='utf-8')
if 'InGameNR = 0' not in settings_source or 'ExternalHostNR = 2' not in settings_source:
    fail('v0.2.8 direct in-game/default and external-host fallback backend modes are missing')

# v0.2.8 direct in-game mount and guide provenance invariants.
ingame = (ROOT / 'src/neural/ingame_nr.cpp').read_text(encoding='utf-8')
streamline = (ROOT / 'src/neural/streamline_nr.cpp').read_text(encoding='utf-8')
tracker = (ROOT / 'src/gpu/d3d11_resource_tracker.cpp').read_text(encoding='utf-8')
camera_tracker = (ROOT / 'src/gpu/d3d11_camera_tracker.cpp').read_text(encoding='utf-8')
camera_shader = (ROOT / 'shaders/camera_motion.hlsl').read_text(encoding='utf-8')
for token in ['createStreamlineNR', 'createNgxNR', 'NeuralExecutionLocation::InGame', 'NeuralBackendKind::SignedFeature18']:
    if token not in ingame:
        fail(f'v0.2.8 in-game NR mount missing: {token}')
for token in ['slInit', 'slSetD3DDevice', 'slGetNewFrameToken', 'slSetConstants', 'kFeatureDLSS_NR', 'slEvaluateFeature', 'ID3D12GraphicsCommandList']:
    if token not in streamline:
        fail(f'v0.2.8 Streamline feature-1004 mount missing: {token}')
for token in ['D3D11_RESOURCE_MISC_SHARED_NTHANDLE', 'D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX', 'OpenSharedHandle']:
    if token not in streamline:
        fail(f'v0.2.8 Streamline GPU interop missing: {token}')
for token in ['cameramotionvectors', 'bestMotionCandidate', 'bestDepthCandidate', 'finalizeFrame']:
    if token not in tracker.lower() if token == 'cameramotionvectors' else token not in tracker:
        fail(f'v0.2.8 D3D11 resource tracker missing: {token}')
for token in ['D3DReflect', 'classifyMatrixBindingName', 'previousViewProjection', 'currentViewProjection']:
    if token not in camera_tracker:
        fail(f'v0.2.8 camera constant tracker missing: {token}')
if 'prevPixel-currentPixel' not in camera_shader.replace(' ', ''):
    fail('v0.2.8 camera motion shader does not emit current->previous pixel vectors')
if 'project(UniversalDLSS5 VERSION 0.2.8' not in cmake_root:
    fail('CMake project version is not v0.2.8')
build_script=(ROOT/'BUILD_WINDOWS.bat').read_text(encoding='utf-8',errors='ignore')
if '-DUDLSS_WITH_STREAMLINE=ON' not in build_script:
    fail('x64 Windows build does not enable Streamline headers for the in-game feature-1004 mount')

shared = (ROOT / 'include/udlss/shared_control.hpp').read_text(encoding='utf-8')
if 'kControlAbi = 16' not in shared:
    fail('scheduler/multipass diagnostics layout must use control ABI 16')
for token in ['stageMask', 'failureStage', 'neuralFrames', 'neuralActive']:
    if token not in shared:
        fail(f'missing v0.2 runtime diagnostic field: {token}')

diag = (ROOT / 'include/udlss/runtime_diagnostics.hpp').read_text(encoding='utf-8')
for token in ['SnippetLoaded', 'SnippetInitialized', 'FeatureCreated', 'FeatureEvaluated', 'OutputComposited']:
    if token not in diag:
        fail(f'missing pipeline diagnostic stage: {token}')

ui = (ROOT / 'src/controller/ui.cpp').read_text(encoding='utf-8')
for token in ['IDC_COPY_DIAGNOSTICS', 'IDC_SAVE_DIAGNOSTICS', 'formatPipelineStages', 'WM_GETMINMAXINFO', 'WC_COMBOBOXEXW', 'Direct in-game NR (recommended)', 'Execution location: ', 'Native motion candidate: ', 'Camera matrices: ']:
    if token not in ui:
        fail(f'missing redesigned controller UI capability: {token}')

pipeline = (ROOT / 'src/gpu/d3d11_pipeline.cpp').read_text(encoding='utf-8')
for shader in ['convert.hlsl','downsample.hlsl','flow.hlsl','nvof_unpack.hlsl','motion.hlsl','mask.hlsl','depth_convert.hlsl','post.hlsl','blit.hlsl']:
    if shader not in pipeline and shader != 'blit.hlsl':
        fail(f'pipeline no longer references expected shader: {shader}')
    if not (ROOT / 'shaders' / shader).is_file():
        fail(f'missing shader source: shaders/{shader}')



# v0.2.8 retains the v0.2.7 quality-guide invariants.
for token in ['GuideProbeResult guide', 'guideExtractor_.probe', 'guideExtractor_.prepareDepth',
              'nrControlMaskTex_', 'fr.controlMask=explicitControlMask?nrControlMaskTex_.Get():nullptr', 'guide.cameraCut']:
    if token not in pipeline:
        fail(f'v0.2.8 D3D11 guide pipeline missing: {token}')
flow = (ROOT / 'shaders/flow.hlsl').read_text(encoding='utf-8')
motion = (ROOT / 'shaders/motion.hlsl').read_text(encoding='utf-8')
mask = (ROOT / 'shaders/mask.hlsl').read_text(encoding='utf-8')
if 'best==0?1:conf' in flow or 'length2' not in flow or 'abs(e-best)<=' not in flow:
    fail('v0.2.8 ambiguous-flow tie-break/confidence fix is missing')
if 'f.z<ConfidenceThreshold' not in motion or 'StaticDeadzone' not in motion:
    fail('v0.2.8 motion confidence/deadzone rejection is missing')
if '1.0-unreliable' not in mask or 'nrApplication' not in mask:
    fail('v0.2.8 NR application-mask polarity is missing')
if ('Texture2D<float2> Motion' not in mask or 'Motion.Load(int3(p,0))' not in mask or
        'Prev.Load(int3(prevPos,0))' not in mask or 'Prev.Load(int3(p,0))' in mask or
        'inBounds' not in mask):
    fail('v0.2.8 ControlMask temporal residual is not motion-reprojected')
if 'Debug view: ' not in ui or 'diagnostic visualization' not in ui:
    fail('v0.2.8 diagnostics do not identify non-final debug views')
extractor = (ROOT / 'src/gpu/d3d11_guide_extractor.cpp').read_text(encoding='utf-8')
for token in ['OMGetRenderTargets', 'UniversalDLSS5.GameGuides.dll', 'GameGuide_MotionPixelCurrentToPrevious', 'GameGuide_ControlMaskIsNrApplication']:
    if token not in extractor:
        fail(f'v0.2.8 conservative game-guide extraction missing: {token}')
for token in ['gameDepthActive', 'controlMaskActive', 'temporalResetThisFrame', 'guideFields', 'nativeMotionCandidateId', 'nativeMotionCandidateScore', 'cameraCurrentValid', 'cameraPreviousValid', 'cameraConfidence', 'temporalReason']:
    if token not in shared:
        fail(f'v0.2.8 guide diagnostic field missing: {token}')

print('PASS: package structure, pinned dependencies, shaders, and runtime redistribution policy verified')
