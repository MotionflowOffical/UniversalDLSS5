#include "ui.hpp"
#include "processes.hpp"
#include "inject_client.hpp"
#include "udlss/shared_control.hpp"
#include "udlss/profile.hpp"
#include "udlss/runtime_policy.hpp"
#include "udlss/runtime_diagnostics.hpp"

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <cwctype>
#include <array>

#pragma comment(lib,"comctl32.lib")
namespace fs = std::filesystem;

namespace udlss::controller {
namespace {

enum : int {
    IDC_PROCESS=100, IDC_REFRESH, IDC_ATTACH, IDC_DETACH, IDC_RUNTIME, IDC_BROWSE, IDC_STATUS,
    IDC_ENABLE, IDC_TREE, IDC_UI_PROTECT, IDC_CTRL_MASK, IDC_INVERT_Y,
    IDC_SECONDARY, IDC_ON12, IDC_UNSUPPORTED, IDC_RESET_HISTORY, IDC_VALIDATE_RUNTIME,
    IDC_BACKEND, IDC_MOTION, IDC_LATENCY, IDC_DOWNSAMPLE, IDC_PRESET,
    IDC_SHOW_ALL, IDC_RETRY_NEURAL, IDC_PROCESS_DETAIL, IDC_DIAGNOSTICS, IDC_COPY_DIAGNOSTICS, IDC_SAVE_DIAGNOSTICS, IDC_BADGE, IDC_TABS, IDC_DOWNSAMPLE_LABEL, IDC_ADVANCED_NOTE,
    IDC_NR_STYLE, IDC_NR_PRESET, IDC_DEPTH_MODE, IDC_DEBUG_VIEW, IDC_AUTO_MASK, IDC_UI_CORRECTION, IDC_RESET_GAP, IDC_GAME_DEPTH, IDC_GAME_ADAPTER,
    IDC_NR_STYLE_LABEL, IDC_NR_PRESET_LABEL, IDC_DEPTH_MODE_LABEL, IDC_DEBUG_VIEW_LABEL, IDC_DEBUG_NOTE,
    IDC_SHARP=200, IDC_EXPOSURE, IDC_TEMPORAL, IDC_MOTIONSCALE, IDC_CONF, IDC_DISOCC,
    IDC_TEXT, IDC_UIP, IDC_MASKSTR, IDC_CLAMP, IDC_REACTIVE, IDC_EDGE, IDC_RADIUS,
    IDC_NR_INTENSITY, IDC_NR_TONE, IDC_NR_STRUCTURE, IDC_NR_SKIN, IDC_NR_PAPER, IDC_NR_TRANSFER, IDC_NR_COLOR,
    IDC_MOTION_X, IDC_MOTION_Y, IDC_DEADZONE, IDC_DEBUG_SPLIT
};

struct Slider {
    int id;
    const wchar_t* name;
    int min;
    int max;
    float scale;
};

static constexpr Slider kSliders[] = {
    {IDC_SHARP,L"Sharpness",0,1000,1000.f},
    {IDC_EXPOSURE,L"Exposure",25,400,100.f},
    {IDC_TEMPORAL,L"DLSS neural mix",0,1000,1000.f},
    {IDC_MOTIONSCALE,L"Motion scale",0,400,100.f},
    {IDC_CONF,L"Flow confidence",0,1000,1000.f},
    {IDC_DISOCC,L"Disocclusion",0,1000,1000.f},
    {IDC_TEXT,L"Text protection",0,1000,1000.f},
    {IDC_UIP,L"UI protection",0,1000,1000.f},
    {IDC_MASKSTR,L"Control-mask strength",0,1000,1000.f},
    {IDC_CLAMP,L"History clamp",0,1000,1000.f},
    {IDC_REACTIVE,L"Reactive strength",0,1000,1000.f},
    {IDC_EDGE,L"Edge threshold",0,1000,1000.f},
    {IDC_RADIUS,L"Flow search radius",1,12,1.f},
    {IDC_NR_INTENSITY,L"NR intensity",0,200,100.f},
    {IDC_NR_TONE,L"Tone intensity",0,200,100.f},
    {IDC_NR_STRUCTURE,L"Structure intensity",0,200,100.f},
    {IDC_NR_SKIN,L"Skin structure",-100,200,100.f},
    {IDC_NR_PAPER,L"Paper white",10,1600,100.f},
    {IDC_NR_TRANSFER,L"Transfer strength",0,200,100.f},
    {IDC_NR_COLOR,L"Color strength",0,400,100.f},
    {IDC_MOTION_X,L"Motion scale X",-400,400,100.f},
    {IDC_MOTION_Y,L"Motion scale Y",-400,400,100.f},
    {IDC_DEADZONE,L"Static-motion deadzone",0,800,100.f},
    {IDC_DEBUG_SPLIT,L"Original / NR split",0,1000,1000.f}
};

struct PickerEntry {
    ProcessInfo root;
    std::wstring displayName;
    std::size_t processCount{1};
    std::size_t visibleWindowCount{};
    bool anyDxgi{};
    bool grouped{};
};

enum class BadgeState { Waiting, Active, Passthrough, Error };

struct State {
    HINSTANCE inst{};
    HWND hwnd{}, process{}, processCombo{}, processDetail{}, runtime{}, status{}, backend{}, motion{}, latency{}, downsample{}, preset{}, nrStyle{}, nrPreset{}, depthMode{}, debugView{};
    HWND diagnostics{}, badge{}, tabs{}, applicationGroup{}, neuralGroup{}, settingsGroup{}, diagnosticsGroup{};
    HWND copyDiagnostics{}, saveDiagnostics{};
    HIMAGELIST processImages{};
    HFONT font{};
    HBRUSH activeBrush{}, passthroughBrush{}, waitingBrush{}, errorBrush{};
    std::vector<PickerEntry> entries;
    std::unordered_set<DWORD> injected;
    DWORD rootPid{};
    std::wstring rootPath;
    std::wstring appDir;
    std::wstring lastDiagnostic;
    Settings settings = defaultSettings();
    SharedControl shared;
    BadgeState badgeState{BadgeState::Waiting};
    bool profileDirty{};
    bool showAllProcesses{};
} g;

std::wstring applicationDirectory() {
    wchar_t p[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr,p,_countof(p));
    return fs::path(std::wstring(p,n)).parent_path().wstring();
}

std::wstring profilePath(const std::wstring& exe) {
    wchar_t base[32768]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA",base,_countof(base));
    std::uint64_t hash = 1469598103934665603ull;
    for (wchar_t c : exe) {
        const wchar_t d = static_cast<wchar_t>(towlower(c));
        hash ^= static_cast<std::uint16_t>(d);
        hash *= 1099511628211ull;
    }
    std::wstringstream name;
    name << std::hex << hash;
    return (fs::path(n ? std::wstring(base,n) : g.appDir) / L"UniversalDLSS5" / L"profiles" / (name.str()+L".ini")).wstring();
}

HWND makeControlEx(DWORD ex,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id) {
    HWND control=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,g.hwnd,(HMENU)(INT_PTR)id,g.inst,nullptr);
    if(control && g.font) SendMessageW(control,WM_SETFONT,(WPARAM)g.font,TRUE);
    return control;
}
HWND makeControl(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id) {
    return makeControlEx(0,cls,text,style,x,y,w,h,id);
}

void makeLabel(const wchar_t* text,int x,int y,int w=170,int id=0) { makeControl(L"STATIC",text,0,x,y,w,20,id); }
void makeGroup(const wchar_t* text,int x,int y,int w,int h) { makeControl(L"BUTTON",text,BS_GROUPBOX,x,y,w,h,0); }
void comboAdd(HWND h,const wchar_t* text) { SendMessageW(h,CB_ADDSTRING,0,(LPARAM)text); }
float sliderValue(int id,float scale) { return (float)SendDlgItemMessageW(g.hwnd,id,TBM_GETPOS,0,0)/scale; }
void setSlider(int id,float value,float scale) { SendDlgItemMessageW(g.hwnd,id,TBM_SETPOS,TRUE,(LPARAM)(int)(value*scale+0.5f)); }

const wchar_t* archName(Arch a) {
    switch(a){case Arch::X86:return L"x86";case Arch::X64:return L"x64";case Arch::Arm64:return L"ARM64";default:return L"unknown arch";}
}

std::wstring rawDisplayName(const ProcessInfo& p) {
    std::wstring n=p.name;
    if(n.size()>4 && _wcsicmp(n.c_str()+n.size()-4,L".exe")==0) n.resize(n.size()-4);
    if(!n.empty()) n[0]=static_cast<wchar_t>(std::towupper(n[0]));
    return n.empty()?L"Process":n;
}

int addExecutableIcon(const std::wstring& path) {
    SHFILEINFOW info{};
    HICON icon=nullptr;
    if(!path.empty() && SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_SMALLICON)) icon=info.hIcon;
    if(!icon) icon=LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    const int index=ImageList_AddIcon(g.processImages,icon);
    if(info.hIcon) DestroyIcon(info.hIcon);
    return index<0?0:index;
}

void saveProfileIfDirty() {
    if (!g.profileDirty || g.rootPath.empty()) return;
    if (saveProfileFile(profilePath(g.rootPath),g.settings)) g.profileDirty=false;
}

void writeLive(bool persist=true) {
    normalize(g.settings);
    g.shared.writeSettings(g.settings);
    if (persist && !g.rootPath.empty()) g.profileDirty=true;
}

void updateSliderCaptions() {
    for (const auto& sl : kSliders) {
        const float v = sliderValue(sl.id,sl.scale);
        wchar_t text[128]{};
        if (sl.scale==1.0f) swprintf_s(text,L"%s: %u",sl.name,(unsigned)v);
        else swprintf_s(text,L"%s: %.3f",sl.name,v);
        SetDlgItemTextW(g.hwnd,1000+sl.id,text);
    }
}

void markCustomPreset() { if (g.preset) SendMessageW(g.preset,CB_SETCURSEL,5,0); }

void readControls(bool fromSlider=false) {
    g.settings.enabled = IsDlgButtonChecked(g.hwnd,IDC_ENABLE)==BST_CHECKED;
    g.settings.attachProcessTree = IsDlgButtonChecked(g.hwnd,IDC_TREE)==BST_CHECKED;
    g.settings.protectUI = IsDlgButtonChecked(g.hwnd,IDC_UI_PROTECT)==BST_CHECKED;
    g.settings.useControlMask = IsDlgButtonChecked(g.hwnd,IDC_CTRL_MASK)==BST_CHECKED;
    g.settings.invertMotionY = IsDlgButtonChecked(g.hwnd,IDC_INVERT_Y)==BST_CHECKED;
    g.settings.processSecondarySwapchains = IsDlgButtonChecked(g.hwnd,IDC_SECONDARY)==BST_CHECKED;
    g.settings.allowD3D11On12 = IsDlgButtonChecked(g.hwnd,IDC_ON12)==BST_CHECKED;
    g.settings.attemptUnsupportedHardware = IsDlgButtonChecked(g.hwnd,IDC_UNSUPPORTED)==BST_CHECKED;
    g.settings.nrAutoMask = IsDlgButtonChecked(g.hwnd,IDC_AUTO_MASK)==BST_CHECKED;
    g.settings.nrUiCorrection = IsDlgButtonChecked(g.hwnd,IDC_UI_CORRECTION)==BST_CHECKED;
    g.settings.resetOnTemporalGap = IsDlgButtonChecked(g.hwnd,IDC_RESET_GAP)==BST_CHECKED;
    g.settings.useGameDepth = IsDlgButtonChecked(g.hwnd,IDC_GAME_DEPTH)==BST_CHECKED;
    g.settings.loadGameGuideAdapter = IsDlgButtonChecked(g.hwnd,IDC_GAME_ADAPTER)==BST_CHECKED;

    g.settings.backend = (BackendMode)std::max<LRESULT>(0,SendMessageW(g.backend,CB_GETCURSEL,0,0));
    g.settings.motionSource = (MotionSource)std::max<LRESULT>(0,SendMessageW(g.motion,CB_GETCURSEL,0,0));
    g.settings.latencyMode = (LatencyMode)std::max<LRESULT>(0,SendMessageW(g.latency,CB_GETCURSEL,0,0));
    g.settings.nrStyle = (std::uint32_t)std::max<LRESULT>(0,SendMessageW(g.nrStyle,CB_GETCURSEL,0,0));
    g.settings.nrPreset = (std::uint32_t)std::max<LRESULT>(0,SendMessageW(g.nrPreset,CB_GETCURSEL,0,0));
    g.settings.depthMode = (DepthGuideMode)std::max<LRESULT>(0,SendMessageW(g.depthMode,CB_GETCURSEL,0,0));
    g.settings.debugView = (DebugView)std::max<LRESULT>(0,SendMessageW(g.debugView,CB_GETCURSEL,0,0));
    const int downsample=(int)SendMessageW(g.downsample,CB_GETCURSEL,0,0);
    g.settings.flowDownsample=1u<<std::clamp(downsample,0,3);

    g.settings.sharpness=sliderValue(IDC_SHARP,1000);
    g.settings.exposure=sliderValue(IDC_EXPOSURE,100);
    g.settings.temporalStrength=sliderValue(IDC_TEMPORAL,1000);
    g.settings.motionScale=sliderValue(IDC_MOTIONSCALE,100);
    g.settings.flowConfidenceThreshold=sliderValue(IDC_CONF,1000);
    g.settings.disocclusionThreshold=sliderValue(IDC_DISOCC,1000);
    g.settings.textProtection=sliderValue(IDC_TEXT,1000);
    g.settings.uiProtection=sliderValue(IDC_UIP,1000);
    g.settings.controlMaskStrength=sliderValue(IDC_MASKSTR,1000);
    g.settings.historyClamp=sliderValue(IDC_CLAMP,1000);
    g.settings.reactiveStrength=sliderValue(IDC_REACTIVE,1000);
    g.settings.edgeThreshold=sliderValue(IDC_EDGE,1000);
    g.settings.flowSearchRadius=(std::uint32_t)sliderValue(IDC_RADIUS,1);
    g.settings.nrIntensity=sliderValue(IDC_NR_INTENSITY,100);g.settings.nrTone=sliderValue(IDC_NR_TONE,100);g.settings.nrStructure=sliderValue(IDC_NR_STRUCTURE,100);g.settings.nrSkinStructure=sliderValue(IDC_NR_SKIN,100);
    g.settings.nrPaperWhite=sliderValue(IDC_NR_PAPER,100);g.settings.nrTransferStrength=sliderValue(IDC_NR_TRANSFER,100);g.settings.nrColorStrength=sliderValue(IDC_NR_COLOR,100);
    g.settings.motionScaleX=sliderValue(IDC_MOTION_X,100);g.settings.motionScaleY=sliderValue(IDC_MOTION_Y,100);g.settings.staticMotionDeadzone=sliderValue(IDC_DEADZONE,100);g.settings.debugSplit=sliderValue(IDC_DEBUG_SPLIT,1000);
    if (fromSlider) markCustomPreset();
    updateSliderCaptions();
    writeLive();
}

void applyControls() {
    CheckDlgButton(g.hwnd,IDC_ENABLE,g.settings.enabled?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_TREE,g.settings.attachProcessTree?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_UI_PROTECT,g.settings.protectUI?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_CTRL_MASK,g.settings.useControlMask?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_INVERT_Y,g.settings.invertMotionY?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_SECONDARY,g.settings.processSecondarySwapchains?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_ON12,g.settings.allowD3D11On12?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_UNSUPPORTED,g.settings.attemptUnsupportedHardware?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(g.hwnd,IDC_AUTO_MASK,g.settings.nrAutoMask?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(g.hwnd,IDC_UI_CORRECTION,g.settings.nrUiCorrection?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(g.hwnd,IDC_RESET_GAP,g.settings.resetOnTemporalGap?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(g.hwnd,IDC_GAME_DEPTH,g.settings.useGameDepth?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(g.hwnd,IDC_GAME_ADAPTER,g.settings.loadGameGuideAdapter?BST_CHECKED:BST_UNCHECKED);
    SendMessageW(g.backend,CB_SETCURSEL,(WPARAM)g.settings.backend,0);
    SendMessageW(g.motion,CB_SETCURSEL,(WPARAM)g.settings.motionSource,0);
    SendMessageW(g.latency,CB_SETCURSEL,(WPARAM)g.settings.latencyMode,0);
    SendMessageW(g.nrStyle,CB_SETCURSEL,(WPARAM)g.settings.nrStyle,0);SendMessageW(g.nrPreset,CB_SETCURSEL,(WPARAM)g.settings.nrPreset,0);SendMessageW(g.depthMode,CB_SETCURSEL,(WPARAM)g.settings.depthMode,0);SendMessageW(g.debugView,CB_SETCURSEL,(WPARAM)g.settings.debugView,0);
    int d=0; for(auto x=g.settings.flowDownsample;x>1;x>>=1) ++d;
    SendMessageW(g.downsample,CB_SETCURSEL,d,0);

    setSlider(IDC_SHARP,g.settings.sharpness,1000);
    setSlider(IDC_EXPOSURE,g.settings.exposure,100);
    setSlider(IDC_TEMPORAL,g.settings.temporalStrength,1000);
    setSlider(IDC_MOTIONSCALE,g.settings.motionScale,100);
    setSlider(IDC_CONF,g.settings.flowConfidenceThreshold,1000);
    setSlider(IDC_DISOCC,g.settings.disocclusionThreshold,1000);
    setSlider(IDC_TEXT,g.settings.textProtection,1000);
    setSlider(IDC_UIP,g.settings.uiProtection,1000);
    setSlider(IDC_MASKSTR,g.settings.controlMaskStrength,1000);
    setSlider(IDC_CLAMP,g.settings.historyClamp,1000);
    setSlider(IDC_REACTIVE,g.settings.reactiveStrength,1000);
    setSlider(IDC_EDGE,g.settings.edgeThreshold,1000);
    setSlider(IDC_RADIUS,(float)g.settings.flowSearchRadius,1);
    setSlider(IDC_NR_INTENSITY,g.settings.nrIntensity,100);setSlider(IDC_NR_TONE,g.settings.nrTone,100);setSlider(IDC_NR_STRUCTURE,g.settings.nrStructure,100);setSlider(IDC_NR_SKIN,g.settings.nrSkinStructure,100);setSlider(IDC_NR_PAPER,g.settings.nrPaperWhite,100);setSlider(IDC_NR_TRANSFER,g.settings.nrTransferStrength,100);setSlider(IDC_NR_COLOR,g.settings.nrColorStrength,100);setSlider(IDC_MOTION_X,g.settings.motionScaleX,100);setSlider(IDC_MOTION_Y,g.settings.motionScaleY,100);setSlider(IDC_DEADZONE,g.settings.staticMotionDeadzone,100);setSlider(IDC_DEBUG_SPLIT,g.settings.debugSplit,1000);
    updateSliderCaptions();
}

void setProcessDetail(const PickerEntry* entry) {
    if(!entry) { SetWindowTextW(g.processDetail,L"No application selected."); return; }
    std::wstringstream out;
    if(entry->grouped) {
        out << entry->processCount << L" process" << (entry->processCount==1?L"":L"es");
        if(entry->visibleWindowCount>1) out << L" · " << entry->visibleWindowCount << L" windows";
    } else out << L"PID " << entry->root.pid;
    out << L" · " << archName(entry->root.arch);
    if(entry->anyDxgi || entry->root.hasDxgi) out << L" · GPU/DXGI detected";
    else out << L" · waiting for DXGI";
    if(entry->root.blocksThirdPartyModules) out << L" · signed-module policy (CIG)";
    SetWindowTextW(g.processDetail,out.str().c_str());
}

void refresh() {
    const DWORD keepPid=g.rootPid;
    const std::wstring keepPath=g.rootPath;
    g.entries.clear();
    if(g.processImages) ImageList_RemoveAll(g.processImages);
    if(g.processCombo) SendMessageW(g.processCombo,CB_RESETCONTENT,0,0);

    if(g.showAllProcesses) {
        for(auto& p:enumerateProcesses()) {
            if(!p.accessible || p.path.empty()) continue;
            PickerEntry e{}; e.root=std::move(p); e.displayName=rawDisplayName(e.root); e.anyDxgi=e.root.hasDxgi; e.grouped=false;
            g.entries.push_back(std::move(e));
        }
    } else {
        for(auto& app:enumerateApplications()) {
            PickerEntry e{}; e.root=std::move(app.root); e.displayName=std::move(app.displayName); e.processCount=app.processCount;
            e.visibleWindowCount=app.visibleWindowCount; e.anyDxgi=app.anyDxgi; e.grouped=true;
            g.entries.push_back(std::move(e));
        }
    }

    int sel=-1;
    for(size_t i=0;i<g.entries.size();++i) {
        auto& e=g.entries[i];
        std::wstring text=e.displayName;
        if(g.showAllProcesses) text+=L"  ["+std::to_wstring(e.root.pid)+L"]";
        const int image=addExecutableIcon(e.root.path);
        COMBOBOXEXITEMW item{}; item.mask=CBEIF_TEXT|CBEIF_IMAGE|CBEIF_SELECTEDIMAGE|CBEIF_LPARAM;
        item.iItem=-1; item.pszText=text.data(); item.iImage=image; item.iSelectedImage=image; item.lParam=(LPARAM)i;
        SendMessageW(g.process,CBEM_INSERTITEMW,0,(LPARAM)&item);
        if((!keepPath.empty() && _wcsicmp(keepPath.c_str(),e.root.path.c_str())==0) || (keepPath.empty() && e.root.pid==keepPid)) sel=(int)i;
    }
    if(sel<0 && !g.entries.empty()) sel=0;
    if(sel>=0) SendMessageW(g.processCombo,CB_SETCURSEL,sel,0);
    setProcessDetail(sel>=0?&g.entries[(size_t)sel]:nullptr);
}

PickerEntry* selectedEntry() {
    if(!g.processCombo) return nullptr;
    const int i=(int)SendMessageW(g.processCombo,CB_GETCURSEL,0,0);
    return i>=0 && (size_t)i<g.entries.size()?&g.entries[(size_t)i]:nullptr;
}

ProcessInfo* selected() { auto* e=selectedEntry(); return e?&e->root:nullptr; }

void selectChanged() {
    auto* entry=selectedEntry();
    if(!entry) return;
    auto* p=&entry->root;
    saveProfileIfDirty();
    g.rootPid=p->pid;
    g.rootPath=p->path;
    setProcessDetail(entry);
    if(!g.rootPath.empty()) {
        Settings s;
        g.settings=loadProfileFile(profilePath(g.rootPath),s)?s:defaultSettings();
        applyControls();
        SendMessageW(g.preset,CB_SETCURSEL,5,0);
        writeLive(false);
    }
    if(p->blocksThirdPartyModules) SetWindowTextW(g.status,L"Selected application has a signed-module policy (CIG) on one or more processes. Those protected processes will be skipped; no mitigation bypass is attempted.");
}

PickerEntry* selectedEntry();
std::wstring runtimeFolderFromUi();

void setStatusText(const std::wstring& text) { if(g.status) SetWindowTextW(g.status,text.c_str()); }

const wchar_t* badgeText(BadgeState state) {
    switch(state){
    case BadgeState::Active:return L"ACTIVE";
    case BadgeState::Passthrough:return L"PASSTHROUGH";
    case BadgeState::Error:return L"ERROR";
    default:return L"WAITING";
    }
}

void setBadge(BadgeState state) {
    g.badgeState=state;
    if(g.badge){SetWindowTextW(g.badge,badgeText(state));InvalidateRect(g.badge,nullptr,TRUE);}
}

const wchar_t* graphicsApiText(GraphicsApi api) {
    switch(api){case GraphicsApi::D3D11:return L"D3D11";case GraphicsApi::D3D12:return L"D3D12";default:return L"Unknown";}
}

const wchar_t* neuralApiText(NeuralExecutionApi api) {
    return api==NeuralExecutionApi::D3D12?L"D3D12":L"None";
}

const wchar_t* runtimeStateText(RuntimeState state) {
    switch(state){
    case RuntimeState::Injected:return L"Injected";
    case RuntimeState::Hooked:return L"Hooked";
    case RuntimeState::Processing:return L"Processing";
    case RuntimeState::Bypassed:return L"Passthrough";
    case RuntimeState::Error:return L"Error";
    case RuntimeState::Unloading:return L"Unloading";
    default:return L"Idle";
    }
}

std::wstring selectedApplicationName() {
    auto* entry=selectedEntry();
    return entry?entry->displayName:L"(none)";
}

const wchar_t* debugViewText(DebugView view) {
    switch(view){
    case DebugView::Original:return L"Original";
    case DebugView::Split:return L"Original / NR split";
    case DebugView::Difference:return L"Difference";
    case DebugView::Motion:return L"Motion vectors";
    case DebugView::MotionConfidence:return L"Motion confidence";
    case DebugView::ControlMask:return L"Control mask";
    case DebugView::Depth:return L"Depth guide";
    default:return L"Final output";
    }
}

std::wstring buildDiagnosticReport(const RuntimeStatus& s) {
    std::wstringstream out;
    out<<L"UniversalDLSS5 diagnostics\r\n";
    out<<L"==========================\r\n";
    out<<L"Application: "<<selectedApplicationName()<<L"\r\n";
    out<<L"Root PID: "<<g.rootPid<<L"\r\n";
    out<<L"Bridge PID: "<<s.pid<<L"\r\n";
    out<<L"State: "<<runtimeStateText(s.state)<<L"\r\n";
    out<<L"Source API: "<<graphicsApiText(s.api)<<L"\r\n";
    out<<L"Neural API: "<<neuralApiText(s.neuralApi)<<L"\r\n";
    out<<L"Execution location: "<<neuralExecutionLocationLabel(s.neuralLocation)<<L"\r\n";
    out<<L"NR backend kind: "<<neuralBackendKindLabel(s.neuralBackendKind)<<L"\r\n";
    out<<L"Backend: "<<(s.backendName[0]?s.backendName:L"(not initialized)")<<L"\r\n";
    out<<L"Runtime folder: "<<runtimeFolderFromUi()<<L"\r\n";
    out<<L"Resolution: "<<s.width<<L" x "<<s.height<<L"\r\n";
    out<<L"Frames: presented="<<s.presentedFrames<<L", pipeline="<<s.processedFrames<<L", neural="<<s.neuralFrames<<L", bypassed="<<s.bypassedFrames<<L"\r\n";
    out<<L"Neural active this frame: "<<(s.neuralActive?L"YES":L"NO")<<L"\r\n";
    out<<L"Estimated FPS: "<<std::fixed<<std::setprecision(1)<<s.estimatedFps<<L"\r\n";
    out<<L"Submit time: "<<std::fixed<<std::setprecision(2)<<s.lastGpuMs<<L" ms\r\n";
    if(s.flowName[0]) out<<L"Motion path: "<<s.flowName<<L"\r\n";
    if(s.nativeMotionCandidateId || s.nativeMotionCandidateScore)
        out<<L"Native motion candidate: id="<<s.nativeMotionCandidateId<<L", score="<<s.nativeMotionCandidateScore<<L"\r\n";
    out<<L"Camera matrices: current="<<(s.cameraCurrentValid?L"YES":L"NO")
       <<L", previous="<<(s.cameraPreviousValid?L"YES":L"NO")
       <<L", confidence="<<s.cameraConfidence<<L"%\r\n";
    out<<L"Temporal history: "<<(s.temporalHistoryValid?L"VALID":L"RESET/INVALID");
    if(s.temporalReason[0]) out<<L" ("<<s.temporalReason<<L")";
    out<<L"\r\n";
    out<<L"Debug view: "<<debugViewText(g.settings.debugView);
    if(g.settings.debugView!=DebugView::Final) out<<L" (diagnostic visualization, not final output)";
    out<<L"\r\n";
    if(s.depthName[0]) out<<L"Depth guide: "<<s.depthName<<L"\r\n";
    if(s.guideAdapterName[0]) out<<L"Game guide adapter: "<<s.guideAdapterName<<L"\r\n";
    if(s.guideFields[0]) out<<L"Guide fields: "<<s.guideFields<<L"\r\n";
    out<<L"Depth source active: "<<(s.gameDepthActive?L"GAME":L"SYNTHETIC")
       <<L"; convention="<<(s.depthInverted?L"INVERTED / reversed-Z":L"NORMAL Z")<<L"\r\n";
    out<<L"NR control mask: "<<(s.controlMaskActive?L"ON":L"OFF")
       <<L"; temporal reset this frame: "<<(s.temporalResetThisFrame?L"YES":L"NO")<<L"\r\n";
    const auto featureText=formatRuntimeFeatureDiagnostics(s.feature);
    if(!featureText.empty()) out<<L"Feature versions: "<<featureText<<L"\r\n";
    if(s.lastResult) out<<L"Last result: 0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill(L'0')<<(std::uint32_t)s.lastResult<<std::dec<<L"\r\n";
    if(s.realGpuArchitecture || s.reportedGpuArchitecture) {
        out<<L"GPU architecture: real=0x"<<std::uppercase<<std::hex<<s.realGpuArchitecture
           <<L", reported=0x"<<s.reportedGpuArchitecture<<std::dec
           <<L", NRHost compatibility="<<(s.architectureCompatibilityActive?L"ACTIVE":L"OFF")<<L"\r\n";
    }
    if(s.attemptUnsupportedHardware) out<<L"Attempt unsupported hardware: ON\r\n";
    if(s.failureStage!=PipelineStage::None) out<<L"Failure stage: "<<pipelineStageLabel(s.failureStage)<<L"\r\n";
    out<<L"\r\nPipeline stages\r\n---------------\r\n"<<formatPipelineStages(s.stageMask,s.failureStage)<<L"\r\n";
    out<<L"\r\nBackend message\r\n---------------\r\n"<<(s.message[0]?s.message:L"(none)")<<L"\r\n";
    if(!g.injected.empty()){
        out<<L"\r\nInjected PIDs: ";
        bool first=true;
        for(DWORD pid:g.injected){if(!first)out<<L", ";first=false;out<<pid;}
        out<<L"\r\n";
    }
    return out.str();
}

bool copyDiagnosticsToClipboard() {
    if(g.lastDiagnostic.empty()) return false;
    if(!OpenClipboard(g.hwnd)) return false;
    EmptyClipboard();
    const SIZE_T bytes=(g.lastDiagnostic.size()+1)*sizeof(wchar_t);
    HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(!mem){CloseClipboard();return false;}
    void* dst=GlobalLock(mem);
    if(!dst){GlobalFree(mem);CloseClipboard();return false;}
    memcpy(dst,g.lastDiagnostic.c_str(),bytes);
    GlobalUnlock(mem);
    if(!SetClipboardData(CF_UNICODETEXT,mem)){GlobalFree(mem);CloseClipboard();return false;}
    CloseClipboard();
    return true;
}

std::string utf8(const std::wstring& text) {
    if(text.empty()) return {};
    const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0,nullptr,nullptr);
    std::string out((std::size_t)std::max(0,count),'\0');
    if(count>0) WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),out.data(),count,nullptr,nullptr);
    return out;
}

std::wstring saveDiagnosticsToFile() {
    if(g.lastDiagnostic.empty()) return {};
    wchar_t local[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,_countof(local));
    fs::path dir=fs::path(n?std::wstring(local,n):g.appDir)/L"UniversalDLSS5"/L"logs";
    std::error_code ec;fs::create_directories(dir,ec);
    SYSTEMTIME t{};GetLocalTime(&t);
    wchar_t name[96]{};
    swprintf_s(name,L"diagnostics-%04u%02u%02u-%02u%02u%02u.txt",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    const fs::path file=dir/name;
    std::ofstream stream(file,std::ios::binary|std::ios::trunc);
    if(!stream) return {};
    const auto bytes=utf8(g.lastDiagnostic);
    stream.write(bytes.data(),(std::streamsize)bytes.size());
    return stream?file.wstring():std::wstring{};
}

std::wstring runtimeFolderFromUi() {
    wchar_t path[32768]{};
    GetWindowTextW(g.runtime,path,_countof(path));
    return path;
}

std::wstring validateRuntimeFolder(const std::wstring& folder) {
    if(folder.empty()) return L"Runtime folder is empty.";
    std::vector<std::wstring_view> missing;
    for(const auto name:requiredDlssNrRuntimeFiles()) {
        std::error_code ec;
        if(!fs::is_regular_file(fs::path(folder)/name,ec)) missing.push_back(name);
    }
    if(!missing.empty()) return formatMissingRuntimeFiles(missing);
    return L"DLSS-NR runtime found: nvngx_dlssnr.dll";
}

bool validateRuntimeForAttach(bool showOk) {
    if(g.settings.backend==BackendMode::Passthrough) return true;
    const auto folder=runtimeFolderFromUi();
    std::vector<std::wstring_view> missing;
    for(const auto name:requiredDlssNrRuntimeFiles()) {
        std::error_code ec;
        if(!fs::is_regular_file(fs::path(folder)/name,ec)) missing.push_back(name);
    }
    if(!missing.empty()) { setStatusText(formatMissingRuntimeFiles(missing)); return false; }
    if(showOk) setStatusText(L"DLSS-NR runtime found: nvngx_dlssnr.dll");
    return true;
}

std::vector<ProcessInfo> attachTargets(const ProcessInfo& root) {
    if(!g.settings.attachProcessTree) return {root};
    auto tree=processTree(root.pid);
    std::vector<ProcessInfo> out;
    out.reserve(tree.size());
    for(const auto& p:tree) {
        // App-centric selection follows the tree, but only the root and graphics children
        // are injection candidates. This avoids renderer/helper duplication in the UI.
        if(p.pid==root.pid || p.hasDxgi) out.push_back(p);
    }
    return out;
}

void pruneInjected() {
    std::unordered_set<DWORD> alive;
    for(const auto& p:enumerateProcesses()) alive.insert(p.pid);
    for(auto it=g.injected.begin();it!=g.injected.end();) {
        if(!alive.contains(*it)) it=g.injected.erase(it); else ++it;
    }
}

void injectCurrentTree(bool report=true) {
    auto* p=selected();
    if(!p) return;
    if(report) readControls(false);
    if(!validateRuntimeForAttach(report)) return;
    g.rootPid=p->pid;
    g.rootPath=p->path;
    g.shared.raw()->rootPid=g.rootPid;
    wchar_t runtime[512]{};
    GetWindowTextW(g.runtime,runtime,_countof(runtime));
    g.shared.setRuntimePath(runtime);
    g.shared.requestUnload(false);
    pruneInjected();

    const auto targets=attachTargets(*p);
    int ok=0,fail=0;
    std::wstring last;
    for(const auto& target:targets) {
        if(g.injected.contains(target.pid)) continue;
        std::wstring message;
        if(injectBridge(target,g.appDir,message)) { g.injected.insert(target.pid); ++ok; }
        else { ++fail; last=target.name+L": "+message; }
    }
    if(report) setStatusText(L"Attach pass: "+std::to_wstring(ok)+L" loaded, "+std::to_wstring(fail)+L" skipped/failed. "+last);
}

void poll() {
    if(g.rootPid && g.settings.attachProcessTree) injectCurrentTree(false);
    saveProfileIfDirty();
    const auto s=g.shared.readStatus();
    if(!s.pid) { setBadge(BadgeState::Waiting); return; }

    BadgeState visual=BadgeState::Waiting;
    if(s.neuralActive) visual=BadgeState::Active;
    else if(s.state==RuntimeState::Error || s.failureStage!=PipelineStage::None) visual=BadgeState::Error;
    else if(s.state==RuntimeState::Bypassed) visual=BadgeState::Passthrough;
    setBadge(visual);

    std::wstringstream summary;
    summary<<graphicsApiText(s.api)<<L" -> "<<neuralApiText(s.neuralApi)
           <<L" · neural "<<s.neuralFrames<<L"/"<<s.presentedFrames
           <<L" · "<<s.width<<L"x"<<s.height;
    if(s.message[0]) summary<<L" · "<<s.message;
    setStatusText(summary.str());

    g.lastDiagnostic=buildDiagnosticReport(s);
    if(g.diagnostics) SetWindowTextW(g.diagnostics,g.lastDiagnostic.c_str());
}

std::wstring pickFolder(HWND owner) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IFileOpenDialog* dialog=nullptr;
    std::wstring out;
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {
        DWORD options=0; dialog->GetOptions(&options); dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
        if(SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item=nullptr;
            if(SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path=nullptr;
                if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { out=path; CoTaskMemFree(path); }
                item->Release();
            }
        }
        dialog->Release();
    }
    CoUninitialize();
    return out;
}

void applySelectedPreset() {
    const int index=(int)SendMessageW(g.preset,CB_GETCURSEL,0,0);
    if(index<0 || index>=5) return;
    applyPreset(g.settings,(TuningPreset)index);
    applyControls();
    writeLive();
    g.shared.requestHistoryReset();
}

bool isSettingsControl(int id) {
    switch(id) {
    case IDC_ENABLE: case IDC_TREE: case IDC_UI_PROTECT: case IDC_CTRL_MASK: case IDC_INVERT_Y:
    case IDC_SECONDARY: case IDC_ON12: case IDC_UNSUPPORTED: case IDC_BACKEND: case IDC_MOTION:
    case IDC_LATENCY: case IDC_DOWNSAMPLE: case IDC_NR_STYLE: case IDC_NR_PRESET: case IDC_DEPTH_MODE: case IDC_DEBUG_VIEW:
    case IDC_AUTO_MASK: case IDC_UI_CORRECTION: case IDC_RESET_GAP: case IDC_GAME_DEPTH: case IDC_GAME_ADAPTER: return true;
    default: return false;
    }
}

const Slider* sliderById(int id) {
    for(const auto& sl:kSliders) if(sl.id==id) return &sl;
    return nullptr;
}

void showControlId(int id,bool show) {
    if(HWND h=GetDlgItem(g.hwnd,id)) ShowWindow(h,show?SW_SHOW:SW_HIDE);
}

void showSliderId(int id,bool show) {
    showControlId(id,show);
    showControlId(1000+id,show);
}

void showSettingsTab(int index) {
    static constexpr std::array<int,4> neural={IDC_NR_INTENSITY,IDC_NR_TONE,IDC_NR_STRUCTURE,IDC_NR_SKIN};
    static constexpr std::array<int,7> temporal={IDC_MOTIONSCALE,IDC_CONF,IDC_MOTION_X,IDC_MOTION_Y,IDC_DEADZONE,IDC_DISOCC,IDC_RADIUS};
    static constexpr std::array<int,12> composition={IDC_SHARP,IDC_EXPOSURE,IDC_TEMPORAL,IDC_TEXT,IDC_UIP,IDC_MASKSTR,IDC_CLAMP,IDC_REACTIVE,IDC_EDGE,IDC_NR_PAPER,IDC_NR_TRANSFER,IDC_NR_COLOR};
    for(int id:neural) showSliderId(id,index==0);
    for(int id:temporal) showSliderId(id,index==1);
    for(int id:composition) showSliderId(id,index==2);
    showSliderId(IDC_DEBUG_SPLIT,index==3);
    for(int id:{IDC_NR_STYLE,IDC_NR_PRESET,IDC_AUTO_MASK,IDC_UI_CORRECTION,IDC_NR_STYLE_LABEL,IDC_NR_PRESET_LABEL}) showControlId(id,index==0);
    for(int id:{IDC_INVERT_Y,IDC_DOWNSAMPLE,IDC_DOWNSAMPLE_LABEL,IDC_DEPTH_MODE,IDC_DEPTH_MODE_LABEL,IDC_RESET_GAP,IDC_GAME_DEPTH,IDC_GAME_ADAPTER}) showControlId(id,index==1);
    for(int id:{IDC_DEBUG_VIEW,IDC_DEBUG_VIEW_LABEL,IDC_DEBUG_NOTE}) showControlId(id,index==3);
    for(int id:{IDC_TREE,IDC_ON12,IDC_SECONDARY,IDC_ADVANCED_NOTE,IDC_UNSUPPORTED}) showControlId(id,index==4);
}

void layoutUi() {
    if(!g.hwnd) return;
    RECT r{};GetClientRect(g.hwnd,&r);
    const int cw=r.right-r.left,ch=r.bottom-r.top;
    if(cw<=0||ch<=0) return;
    const int leftW=650;
    const int mainY=226;
    const int mainH=std::max(400,ch-mainY-12);
    const int diagX=leftW+24;
    const int diagW=std::max(420,cw-diagX-12);

    if(g.applicationGroup) MoveWindow(g.applicationGroup,12,10,cw-24,98,TRUE);
    if(g.neuralGroup) MoveWindow(g.neuralGroup,12,114,cw-24,104,TRUE);
    if(g.settingsGroup) MoveWindow(g.settingsGroup,12,mainY,leftW,mainH,TRUE);
    if(g.diagnosticsGroup) MoveWindow(g.diagnosticsGroup,diagX,mainY,diagW,mainH,TRUE);
    if(g.tabs) MoveWindow(g.tabs,28,306,leftW-32,mainH-94,TRUE);
    if(g.diagnostics) MoveWindow(g.diagnostics,diagX+16,mainY+34,diagW-32,mainH-84,TRUE);
    if(g.copyDiagnostics) MoveWindow(g.copyDiagnostics,diagX+16,mainY+mainH-40,130,28,TRUE);
    if(g.saveDiagnostics) MoveWindow(g.saveDiagnostics,diagX+156,mainY+mainH-40,130,28,TRUE);
    if(g.badge) MoveWindow(g.badge,cw-152,176,124,28,TRUE);
    if(g.status) MoveWindow(g.status,28,178,cw-205,24,TRUE);
    if(g.processDetail) MoveWindow(g.processDetail,28,76,cw-420,22,TRUE);
}

void createSlider(int id,int col,int row) {
    const Slider* sl=sliderById(id);if(!sl)return;
    const int x=40+col*302, y=350+row*58;
    makeLabel(sl->name,x,y,260,1000+id);
    HWND h=makeControl(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS,x,y+20,258,32,id);
    SendMessageW(h,TBM_SETRANGE,TRUE,MAKELONG(sl->min,sl->max));
    SendMessageW(h,TBM_SETTICFREQ,std::max(1,(sl->max-sl->min)/10),0);
}

void makeUi() {
    g.font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g.activeBrush=CreateSolidBrush(RGB(34,128,82));
    g.passthroughBrush=CreateSolidBrush(RGB(168,116,32));
    g.waitingBrush=CreateSolidBrush(RGB(92,99,112));
    g.errorBrush=CreateSolidBrush(RGB(166,54,54));

    g.applicationGroup=makeControl(L"BUTTON",L"Target application",BS_GROUPBOX,12,10,1200,98,0);
    makeLabel(L"Application",28,31,95);
    g.process=makeControl(WC_COMBOBOXEXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL,120,27,490,300,IDC_PROCESS);
    g.processCombo=(HWND)SendMessageW(g.process,CBEM_GETCOMBOCONTROL,0,0);
    if(g.processCombo && g.font) SendMessageW(g.processCombo,WM_SETFONT,(WPARAM)g.font,TRUE);
    g.processImages=ImageList_Create(20,20,ILC_COLOR32|ILC_MASK,16,16);
    SendMessageW(g.process,CBEM_SETIMAGELIST,0,(LPARAM)g.processImages);
    makeControl(L"BUTTON",L"Refresh",0,620,27,82,28,IDC_REFRESH);
    makeControl(L"BUTTON",L"Show all processes",BS_AUTOCHECKBOX,714,31,150,22,IDC_SHOW_ALL);
    makeControl(L"BUTTON",L"Attach",0,885,26,88,29,IDC_ATTACH);
    makeControl(L"BUTTON",L"Detach",0,983,26,88,29,IDC_DETACH);
    makeControl(L"BUTTON",L"Reset history",0,1081,26,110,29,IDC_RESET_HISTORY);
    g.processDetail=makeControl(L"STATIC",L"",SS_LEFT,28,76,800,22,IDC_PROCESS_DETAIL);

    g.neuralGroup=makeControl(L"BUTTON",L"Neural runtime",BS_GROUPBOX,12,114,1200,104,0);
    makeLabel(L"Runtime folder",28,140,120);
    g.runtime=makeControlEx(WS_EX_CLIENTEDGE,L"EDIT",(fs::path(g.appDir)/L"runtime").c_str(),ES_AUTOHSCROLL,150,136,470,25,IDC_RUNTIME);
    makeControl(L"BUTTON",L"Browse...",0,630,135,86,27,IDC_BROWSE);
    makeControl(L"BUTTON",L"Check",0,726,135,78,27,IDC_VALIDATE_RUNTIME);
    makeLabel(L"Backend",822,140,62);
    g.backend=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,884,136,165,180,IDC_BACKEND);
    comboAdd(g.backend,L"Direct in-game NR (recommended)");comboAdd(g.backend,L"Passthrough");comboAdd(g.backend,L"External NR Host (fallback)");
    makeControl(L"BUTTON",L"Retry neural",0,1060,135,130,27,IDC_RETRY_NEURAL);
    g.status=makeControl(L"STATIC",L"Ready. Select an application and attach.",SS_LEFT|SS_NOPREFIX,28,178,930,24,IDC_STATUS);
    g.badge=makeControl(L"STATIC",L"WAITING",SS_CENTER|SS_CENTERIMAGE|WS_BORDER,1065,176,124,28,IDC_BADGE);

    g.settingsGroup=makeControl(L"BUTTON",L"Processing controls",BS_GROUPBOX,12,226,650,520,0);
    makeControl(L"BUTTON",L"Processing enabled",BS_AUTOCHECKBOX,28,250,155,22,IDC_ENABLE);
    makeControl(L"BUTTON",L"Protect UI / text",BS_AUTOCHECKBOX,190,250,145,22,IDC_UI_PROTECT);
    makeControl(L"BUTTON",L"Use control mask",BS_AUTOCHECKBOX,342,250,145,22,IDC_CTRL_MASK);
    makeLabel(L"Preset",28,280,48);
    g.preset=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,78,276,150,180,IDC_PRESET);
    comboAdd(g.preset,L"Default");comboAdd(g.preset,L"Browser / desktop");comboAdd(g.preset,L"2D game");comboAdd(g.preset,L"Video");comboAdd(g.preset,L"Aggressive");comboAdd(g.preset,L"Custom");
    SendMessageW(g.preset,CB_SETCURSEL,5,0);
    makeLabel(L"Motion",240,280,48);
    g.motion=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,292,276,145,180,IDC_MOTION);
    comboAdd(g.motion,L"Auto: native -> camera+depth -> NVOFA -> safe zero");comboAdd(g.motion,L"Optical flow (NVOFA/HLSL experimental)");comboAdd(g.motion,L"Zero");
    makeLabel(L"Latency",450,280,52);
    g.latency=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,505,276,130,180,IDC_LATENCY);
    comboAdd(g.latency,L"Ultra low");comboAdd(g.latency,L"Balanced");comboAdd(g.latency,L"Quality");
    g.tabs=makeControl(WC_TABCONTROLW,L"",WS_TABSTOP,28,306,618,420,IDC_TABS);
    TCITEMW item{};item.mask=TCIF_TEXT;
    wchar_t neural[]=L"Neural model";item.pszText=neural;TabCtrl_InsertItem(g.tabs,0,&item);
    wchar_t temporal[]=L"Temporal guides";item.pszText=temporal;TabCtrl_InsertItem(g.tabs,1,&item);
    wchar_t composition[]=L"Composition";item.pszText=composition;TabCtrl_InsertItem(g.tabs,2,&item);
    wchar_t debug[]=L"Debug";item.pszText=debug;TabCtrl_InsertItem(g.tabs,3,&item);
    wchar_t advanced[]=L"Advanced";item.pszText=advanced;TabCtrl_InsertItem(g.tabs,4,&item);
    TabCtrl_SetCurSel(g.tabs,0);

    // Neural model tab
    createSlider(IDC_NR_INTENSITY,0,0);createSlider(IDC_NR_TONE,1,0);createSlider(IDC_NR_STRUCTURE,0,1);createSlider(IDC_NR_SKIN,1,1);
    makeLabel(L"Style",40,475,80,IDC_NR_STYLE_LABEL);g.nrStyle=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,120,471,170,180,IDC_NR_STYLE);comboAdd(g.nrStyle,L"Default");comboAdd(g.nrStyle,L"Natural");comboAdd(g.nrStyle,L"Cinematic");comboAdd(g.nrStyle,L"3");comboAdd(g.nrStyle,L"4");comboAdd(g.nrStyle,L"5");comboAdd(g.nrStyle,L"6");
    makeLabel(L"Model preset",342,475,100,IDC_NR_PRESET_LABEL);g.nrPreset=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,450,471,150,180,IDC_NR_PRESET);comboAdd(g.nrPreset,L"0");comboAdd(g.nrPreset,L"1");comboAdd(g.nrPreset,L"2");comboAdd(g.nrPreset,L"3");
    makeControl(L"BUTTON",L"Semantic auto-mask",BS_AUTOCHECKBOX,40,515,180,22,IDC_AUTO_MASK);makeControl(L"BUTTON",L"UI correction",BS_AUTOCHECKBOX,342,515,150,22,IDC_UI_CORRECTION);

    // Temporal guides tab
    createSlider(IDC_MOTIONSCALE,0,0);createSlider(IDC_CONF,1,0);createSlider(IDC_MOTION_X,0,1);createSlider(IDC_MOTION_Y,1,1);createSlider(IDC_DEADZONE,0,2);createSlider(IDC_DISOCC,1,2);createSlider(IDC_RADIUS,0,3);
    makeControl(L"BUTTON",L"Invert motion Y",BS_AUTOCHECKBOX,342,545,150,22,IDC_INVERT_Y);
    makeControl(L"STATIC",L"Flow resolution",SS_LEFT,342,580,105,20,IDC_DOWNSAMPLE_LABEL);g.downsample=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,450,576,150,180,IDC_DOWNSAMPLE);comboAdd(g.downsample,L"1x (full)");comboAdd(g.downsample,L"2x");comboAdd(g.downsample,L"4x");comboAdd(g.downsample,L"8x (fastest)");
    makeLabel(L"Depth guide",40,620,95,IDC_DEPTH_MODE_LABEL);g.depthMode=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,140,616,160,180,IDC_DEPTH_MODE);comboAdd(g.depthMode,L"Auto / game depth");comboAdd(g.depthMode,L"Synthetic far");comboAdd(g.depthMode,L"Force normal Z");comboAdd(g.depthMode,L"Force inverted Z");
    makeControl(L"BUTTON",L"Reset after temporal gap",BS_AUTOCHECKBOX,342,620,200,22,IDC_RESET_GAP);makeControl(L"BUTTON",L"Use game depth",BS_AUTOCHECKBOX,40,655,160,22,IDC_GAME_DEPTH);makeControl(L"BUTTON",L"Load GameGuides adapter",BS_AUTOCHECKBOX,240,655,210,22,IDC_GAME_ADAPTER);

    // Composition tab
    createSlider(IDC_SHARP,0,0);createSlider(IDC_EXPOSURE,1,0);createSlider(IDC_TEMPORAL,0,1);createSlider(IDC_TEXT,1,1);createSlider(IDC_UIP,0,2);createSlider(IDC_MASKSTR,1,2);createSlider(IDC_CLAMP,0,3);createSlider(IDC_REACTIVE,1,3);createSlider(IDC_EDGE,0,4);createSlider(IDC_NR_PAPER,1,4);createSlider(IDC_NR_TRANSFER,0,5);createSlider(IDC_NR_COLOR,1,5);

    // Debug tab
    createSlider(IDC_DEBUG_SPLIT,0,0);makeLabel(L"Debug view",342,355,90,IDC_DEBUG_VIEW_LABEL);g.debugView=makeControl(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST,440,351,170,220,IDC_DEBUG_VIEW);comboAdd(g.debugView,L"Final output");comboAdd(g.debugView,L"Original");comboAdd(g.debugView,L"Original / NR split");comboAdd(g.debugView,L"Difference");comboAdd(g.debugView,L"Motion vectors");comboAdd(g.debugView,L"Motion confidence");comboAdd(g.debugView,L"Control mask");comboAdd(g.debugView,L"Depth guide");
    makeControl(L"STATIC",L"GPU-only debug views. Normals/albedo from GameGuides are tracked for diagnostics but are not bound to undocumented NR parameters.",SS_LEFT,40,420,560,60,IDC_DEBUG_NOTE);


    makeControl(L"BUTTON",L"Follow application process tree",BS_AUTOCHECKBOX,42,355,230,22,IDC_TREE);
    makeControl(L"BUTTON",L"Enable D3D11On12 source bridge",BS_AUTOCHECKBOX,42,390,245,22,IDC_ON12);
    makeControl(L"BUTTON",L"Process secondary swapchains",BS_AUTOCHECKBOX,42,425,230,22,IDC_SECONDARY);
    makeControl(L"BUTTON",L"Attempt unsupported hardware",BS_AUTOCHECKBOX,42,460,230,22,IDC_UNSUPPORTED);
    makeControl(L"STATIC",L"Supported source APIs: Direct3D 11 and Direct3D 12. The neural pass always executes through D3D12. Vulkan/OpenGL are reported as unsupported. Protected/CIG processes are skipped; no mitigation bypass is used.",SS_LEFT,42,500,570,72,IDC_ADVANCED_NOTE);

    g.diagnosticsGroup=makeControl(L"BUTTON",L"Diagnostics",BS_GROUPBOX,674,226,550,520,0);
    g.diagnostics=makeControlEx(WS_EX_CLIENTEDGE,L"EDIT",L"Waiting for an injected bridge...",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL,690,260,518,430,IDC_DIAGNOSTICS);
    g.copyDiagnostics=makeControl(L"BUTTON",L"Copy diagnostics",0,690,700,130,28,IDC_COPY_DIAGNOSTICS);
    g.saveDiagnostics=makeControl(L"BUTTON",L"Save diagnostics",0,830,700,130,28,IDC_SAVE_DIAGNOSTICS);

    applyControls();
    showSettingsTab(0);
    refresh();
    selectChanged();
    setBadge(BadgeState::Waiting);
    layoutUi();
}

LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_CREATE:
        g.hwnd=h;
        makeUi();
        SetTimer(h,1,500,nullptr);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize.x=1180;
        info->ptMinTrackSize.y=720;
        return 0;
    }
    case WM_SIZE:
        layoutUi();
        return 0;
    case WM_NOTIFY: {
        const auto* hdr=reinterpret_cast<NMHDR*>(l);
        if(hdr && hdr->idFrom==IDC_TABS && hdr->code==TCN_SELCHANGE){showSettingsTab(TabCtrl_GetCurSel(g.tabs));return 0;}
        break;
    }
    case WM_CTLCOLORSTATIC:
        if((HWND)l==g.badge){
            HDC dc=(HDC)w;SetTextColor(dc,RGB(255,255,255));
            HBRUSH brush=g.waitingBrush;COLORREF color=RGB(92,99,112);
            if(g.badgeState==BadgeState::Active){brush=g.activeBrush;color=RGB(34,128,82);}
            else if(g.badgeState==BadgeState::Passthrough){brush=g.passthroughBrush;color=RGB(168,116,32);}
            else if(g.badgeState==BadgeState::Error){brush=g.errorBrush;color=RGB(166,54,54);}
            SetBkColor(dc,color);return (LRESULT)brush;
        }
        break;
    case WM_COMMAND: {
        const int id=LOWORD(w), code=HIWORD(w);
        if(id==IDC_REFRESH) { refresh(); selectChanged(); return 0; }
        if(id==IDC_PROCESS && code==CBN_SELCHANGE) { selectChanged(); return 0; }
        if(id==IDC_SHOW_ALL) { g.showAllProcesses=IsDlgButtonChecked(g.hwnd,IDC_SHOW_ALL)==BST_CHECKED; refresh(); selectChanged(); return 0; }
        if(id==IDC_ATTACH) { injectCurrentTree(); return 0; }
        if(id==IDC_DETACH) { g.shared.requestUnload(true); g.injected.clear(); setBadge(BadgeState::Waiting); setStatusText(L"Unload requested for injected bridges."); return 0; }
        if(id==IDC_RESET_HISTORY) { g.shared.requestHistoryReset(); setStatusText(L"Temporal history reset requested."); return 0; }
        if(id==IDC_RETRY_NEURAL) { g.shared.requestNeuralRetry(); setBadge(BadgeState::Waiting); setStatusText(L"Neural backend retry requested. The next presented frame will rebuild the selected NR session/host."); return 0; }
        if(id==IDC_COPY_DIAGNOSTICS) { setStatusText(copyDiagnosticsToClipboard()?L"Diagnostics copied to clipboard.":L"No diagnostics available to copy."); return 0; }
        if(id==IDC_SAVE_DIAGNOSTICS) { const auto path=saveDiagnosticsToFile();setStatusText(path.empty()?L"Could not save diagnostics.":L"Saved diagnostics: "+path);return 0; }
        if(id==IDC_BROWSE) {
            const auto path=pickFolder(h);
            if(!path.empty()) { SetWindowTextW(g.runtime,path.c_str()); g.shared.setRuntimePath(path.c_str()); setStatusText(validateRuntimeFolder(path)); }
            return 0;
        }
        if(id==IDC_VALIDATE_RUNTIME) { setStatusText(validateRuntimeFolder(runtimeFolderFromUi())); return 0; }
        if(id==IDC_PRESET && code==CBN_SELCHANGE) { applySelectedPreset(); return 0; }
        if(isSettingsControl(id)) { markCustomPreset(); readControls(false); return 0; }
        break;
    }
    case WM_HSCROLL:
        readControls(true);
        return 0;
    case WM_TIMER:
        poll();
        return 0;
    case WM_DESTROY:
        saveProfileIfDirty();
        g.shared.requestUnload(true);
        if(g.processImages){ImageList_Destroy(g.processImages);g.processImages=nullptr;}
        if(g.font){DeleteObject(g.font);g.font=nullptr;}
        if(g.activeBrush)DeleteObject(g.activeBrush);if(g.passthroughBrush)DeleteObject(g.passthroughBrush);if(g.waitingBrush)DeleteObject(g.waitingBrush);if(g.errorBrush)DeleteObject(g.errorBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

} // namespace

int runUi(HINSTANCE instance,int show) {
    g.inst=instance;
    g.appDir=applicationDirectory();
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES|ICC_USEREX_CLASSES};
    InitCommonControlsEx(&ic);
    if(!g.shared.create()) return 2;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc=windowProc;
    wc.hInstance=instance;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName=L"UniversalDLSS5.Controller";
    RegisterClassExW(&wc);
    HWND h=CreateWindowExW(0,wc.lpszClassName,L"Universal DLSS 5 - Neural Rendering Controller",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1320,820,nullptr,nullptr,instance,nullptr);
    if(!h) return 3;
    ShowWindow(h,show);
    UpdateWindow(h);
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}

} // namespace udlss::controller
