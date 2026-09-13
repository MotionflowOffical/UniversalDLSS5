#include "ui.hpp"
#include "processes.hpp"
#include "inject_client.hpp"
#include "runtime_importer.hpp"
#include "resource.h"
#include "udlss/shared_control.hpp"
#include "udlss/profile.hpp"
#include "udlss/runtime_policy.hpp"
#include "udlss/runtime_diagnostics.hpp"
#include "udlss/renderer_selection_policy.hpp"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace udlss::controller {
namespace {

enum class Page : int { Application=0, Neural, Motion, Composition, Diagnostics, Advanced };
enum class BadgeState { Waiting, Active, Passthrough, Error };
enum class HitKind { Nav, Button, Toggle, Choice, Slider, AppRow };

enum ButtonId : int {
    BTN_REFRESH=1, BTN_ATTACH, BTN_DETACH, BTN_RESET_HISTORY, BTN_BROWSE, BTN_CHECK_RUNTIME,
    BTN_RETRY_NEURAL, BTN_COPY_DIAG, BTN_SAVE_DIAG, BTN_IMPORT_NVIDIA, BTN_OPEN_RUNTIME, BTN_NVIDIA_DOWNLOAD
};
enum ToggleId : int {
    TGL_SHOW_ALL=1, TGL_ENABLE, TGL_UI_PROTECT, TGL_CTRL_MASK, TGL_AUTO_MASK, TGL_UI_CORRECTION,
    TGL_INVERT_Y, TGL_RESET_GAP, TGL_GAME_DEPTH, TGL_GAME_ADAPTER, TGL_TREE, TGL_ON12, TGL_SECONDARY,
    TGL_UNSUPPORTED
};
enum ChoiceId : int {
    CH_PRESET=1, CH_BACKEND, CH_PACING, CH_PASSES, CH_STYLE, CH_MODEL_PRESET,
    CH_MOTION, CH_LATENCY, CH_DOWNSAMPLE, CH_DEPTH, CH_DEBUG, CH_THEME
};
enum SliderId : int {
    SL_NR_INTENSITY=1, SL_NR_TONE, SL_NR_STRUCTURE, SL_NR_SKIN,
    SL_MOTION_SCALE, SL_CONFIDENCE, SL_MOTION_X, SL_MOTION_Y, SL_DEADZONE, SL_DISOCC, SL_RADIUS,
    SL_SHARPNESS, SL_EXPOSURE, SL_TEMPORAL, SL_TEXT, SL_UI, SL_MASK, SL_CLAMP, SL_REACTIVE, SL_EDGE,
    SL_PAPER, SL_TRANSFER, SL_COLOR, SL_DEBUG_SPLIT
};

struct SliderSpec { SliderId id; const wchar_t* label; float lo,hi; int precision; };
static constexpr SliderSpec kSliderSpecs[] = {
    {SL_NR_INTENSITY,L"NR intensity",0.f,2.f,2},{SL_NR_TONE,L"Tone intensity",0.f,2.f,2},
    {SL_NR_STRUCTURE,L"Structure intensity",0.f,2.f,2},{SL_NR_SKIN,L"Skin structure",-1.f,2.f,2},
    {SL_MOTION_SCALE,L"Motion scale",0.f,4.f,2},{SL_CONFIDENCE,L"Flow confidence",0.f,1.f,2},
    {SL_MOTION_X,L"Motion scale X",-4.f,4.f,2},{SL_MOTION_Y,L"Motion scale Y",-4.f,4.f,2},
    {SL_DEADZONE,L"Static-motion deadzone",0.f,8.f,2},{SL_DISOCC,L"Disocclusion threshold",0.f,1.f,2},
    {SL_RADIUS,L"Flow search radius",1.f,12.f,0},{SL_SHARPNESS,L"Sharpness",0.f,1.f,2},
    {SL_EXPOSURE,L"Exposure",0.25f,4.f,2},{SL_TEMPORAL,L"Neural mix",0.f,1.f,2},
    {SL_TEXT,L"Text protection",0.f,1.f,2},{SL_UI,L"UI protection",0.f,1.f,2},
    {SL_MASK,L"Control-mask strength",0.f,1.f,2},{SL_CLAMP,L"History clamp",0.f,1.f,2},
    {SL_REACTIVE,L"Reactive strength",0.f,1.f,2},{SL_EDGE,L"Edge threshold",0.f,1.f,2},
    {SL_PAPER,L"Paper white",0.1f,16.f,2},{SL_TRANSFER,L"Transfer strength",0.f,2.f,2},
    {SL_COLOR,L"Color strength",0.f,4.f,2},{SL_DEBUG_SPLIT,L"Original / NR split",0.f,1.f,2}
};

struct PickerEntry {
    ProcessInfo root;
    std::wstring displayName;
    std::size_t processCount{1};
    std::size_t visibleWindowCount{};
    bool anyDxgi{};
    bool grouped{};
};

struct Hit {
    D2D1_RECT_F rect{};
    HitKind kind{};
    int id{};
    int value{};
    float lo{},hi{};
};

struct Palette {
    D2D1_COLOR_F background{},surface{},surface2{},text{},muted{},border{},accent{},accentSoft{},good{},warn{},bad{},white{};
};

struct State {
    HINSTANCE inst{};
    HWND hwnd{};
    std::wstring appDir;
    std::wstring runtimePath;
    Settings settings=defaultSettings();
    SharedControl shared;

    std::vector<PickerEntry> entries;
    int selectedIndex{-1};
    bool showAllProcesses{};
    int presetIndex{5};
    DWORD rootPid{};
    std::wstring rootPath;
    DWORD primaryRendererPid{};
    std::unordered_set<DWORD> injected;
    std::mutex injectedMutex;
    std::jthread maintenanceThread;
    std::mutex maintenanceMutex;
    ProcessInfo maintenanceRoot{};
    bool maintenanceEnabled{};
    bool maintenanceFollowTree{true};

    BadgeState badgeState{BadgeState::Waiting};
    std::wstring statusText=L"Ready. Select an application and attach.";
    std::wstring lastDiagnostic=L"Waiting for an injected bridge...";
    RuntimeStatus lastRuntimeStatus{};
    bool profileDirty{};

    Page page{Page::Application};
    float contentScroll{};
    float contentMaxHeight{};
    float contentViewportHeight{};
    std::vector<Hit> hits;
    int hoverHit{-1};
    int activeSlider{};
    D2D1_RECT_F activeSliderRect{};
    float activeSliderLo{},activeSliderHi{};

    bool darkTheme{};
    float dpi{96.f};
    Palette palette{};
    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> dwriteFactory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<IDWriteTextFormat> titleFormat,headingFormat,bodyFormat,bodyCenterFormat,smallFormat,smallCenterFormat,monoFormat;
    ComPtr<ID2D1SolidColorBrush> bgBrush,surfaceBrush,surface2Brush,textBrush,mutedBrush,borderBrush,accentBrush,accentSoftBrush,goodBrush,warnBrush,badBrush,whiteBrush;
} g;

constexpr float kSidebarW=190.f;
constexpr float kTopH=78.f;
constexpr float kMargin=20.f;

std::wstring applicationDirectory() {
    wchar_t p[32768]{};
    const DWORD n=GetModuleFileNameW(nullptr,p,_countof(p));
    return fs::path(std::wstring(p,n)).parent_path().wstring();
}

std::wstring userRuntimeDirectory() {
    wchar_t local[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,_countof(local));
    if(n && n<_countof(local)) return (fs::path(std::wstring(local,n))/L"UniversalDLSS5"/L"runtime").wstring();
    return (fs::path(g.appDir)/L"runtime").wstring();
}

std::wstring defaultRuntimeDirectory() {
    const auto besideApp=fs::path(g.appDir)/L"runtime";
    std::error_code ec;
    if(fs::is_regular_file(besideApp/L"nvngx_dlssnr.dll",ec)) return besideApp.wstring();
    return userRuntimeDirectory();
}

std::wstring loadRuntimePathPreference(const std::wstring& fallback) {
    wchar_t value[32768]{};
    DWORD bytes=sizeof(value),type=0;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\UniversalDLSS5",L"RuntimePath",RRF_RT_REG_SZ,&type,value,&bytes)==ERROR_SUCCESS && value[0])
        return value;
    return fallback;
}

void saveRuntimePathPreference(const std::wstring& path) {
    RegSetKeyValueW(HKEY_CURRENT_USER,L"Software\UniversalDLSS5",L"RuntimePath",REG_SZ,path.c_str(),static_cast<DWORD>((path.size()+1)*sizeof(wchar_t)));
}

std::wstring profilePath(const std::wstring& exe) {
    wchar_t base[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,_countof(base));
    std::uint64_t hash=1469598103934665603ull;
    for(wchar_t c:exe){hash^=static_cast<std::uint16_t>(std::towlower(c));hash*=1099511628211ull;}
    std::wstringstream name;name<<std::hex<<hash;
    return (fs::path(n?std::wstring(base,n):g.appDir)/L"UniversalDLSS5"/L"profiles"/(name.str()+L".ini")).wstring();
}

bool systemPrefersDark() {
    DWORD value=1,size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS)
        return value==0;
    return false;
}

bool resolvedDarkTheme() {
    if(g.settings.uiTheme==UiTheme::Dark) return true;
    if(g.settings.uiTheme==UiTheme::Light) return false;
    return systemPrefersDark();
}

D2D1_COLOR_F rgb(unsigned r,unsigned gg,unsigned b,float a=1.f){return D2D1::ColorF(r/255.f,gg/255.f,b/255.f,a);}

void rebuildPalette() {
    g.darkTheme=resolvedDarkTheme();
    if(g.darkTheme){
        g.palette.background=rgb(14,16,19);g.palette.surface=rgb(23,26,31);g.palette.surface2=rgb(31,35,41);
        g.palette.text=rgb(239,241,244);g.palette.muted=rgb(159,166,177);g.palette.border=rgb(53,59,68);
        g.palette.accent=rgb(79,141,255);g.palette.accentSoft=rgb(38,59,91);g.palette.good=rgb(52,184,112);g.palette.warn=rgb(220,157,63);g.palette.bad=rgb(225,87,87);g.palette.white=rgb(255,255,255);
    }else{
        g.palette.background=rgb(246,247,249);g.palette.surface=rgb(255,255,255);g.palette.surface2=rgb(249,250,252);
        g.palette.text=rgb(27,31,36);g.palette.muted=rgb(99,107,118);g.palette.border=rgb(219,224,231);
        g.palette.accent=rgb(45,108,223);g.palette.accentSoft=rgb(229,238,255);g.palette.good=rgb(37,143,83);g.palette.warn=rgb(170,111,27);g.palette.bad=rgb(190,61,61);g.palette.white=rgb(255,255,255);
    }
}

void releaseBrushes(){g.bgBrush.Reset();g.surfaceBrush.Reset();g.surface2Brush.Reset();g.textBrush.Reset();g.mutedBrush.Reset();g.borderBrush.Reset();g.accentBrush.Reset();g.accentSoftBrush.Reset();g.goodBrush.Reset();g.warnBrush.Reset();g.badBrush.Reset();g.whiteBrush.Reset();}
void rebuildBrushes(){
    if(!g.target)return;releaseBrushes();
    g.target->CreateSolidColorBrush(g.palette.background,&g.bgBrush);g.target->CreateSolidColorBrush(g.palette.surface,&g.surfaceBrush);
    g.target->CreateSolidColorBrush(g.palette.surface2,&g.surface2Brush);g.target->CreateSolidColorBrush(g.palette.text,&g.textBrush);
    g.target->CreateSolidColorBrush(g.palette.muted,&g.mutedBrush);g.target->CreateSolidColorBrush(g.palette.border,&g.borderBrush);
    g.target->CreateSolidColorBrush(g.palette.accent,&g.accentBrush);g.target->CreateSolidColorBrush(g.palette.accentSoft,&g.accentSoftBrush);
    g.target->CreateSolidColorBrush(g.palette.good,&g.goodBrush);g.target->CreateSolidColorBrush(g.palette.warn,&g.warnBrush);
    g.target->CreateSolidColorBrush(g.palette.bad,&g.badBrush);g.target->CreateSolidColorBrush(g.palette.white,&g.whiteBrush);
}

HRESULT makeTextFormat(const wchar_t* family,float size,DWRITE_FONT_WEIGHT weight,DWRITE_TEXT_ALIGNMENT align,ComPtr<IDWriteTextFormat>& out){
    if(!g.dwriteFactory)return E_FAIL;
    HRESULT hr=g.dwriteFactory->CreateTextFormat(family,nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&out);
    if(FAILED(hr) && wcscmp(family,L"Segoe UI")!=0) hr=g.dwriteFactory->CreateTextFormat(L"Segoe UI",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&out);
    if(SUCCEEDED(hr)){out->SetTextAlignment(align);out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);out->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);}return hr;
}

bool ensureGraphics(){
    if(!g.d2dFactory){
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory),nullptr,reinterpret_cast<void**>(g.d2dFactory.GetAddressOf()))))return false;
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g.dwriteFactory.GetAddressOf()))))return false;
        makeTextFormat(L"Segoe UI Variable Display",22.f,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING,g.titleFormat);
        makeTextFormat(L"Segoe UI Variable",16.f,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_TEXT_ALIGNMENT_LEADING,g.headingFormat);
        makeTextFormat(L"Segoe UI Variable",14.f,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING,g.bodyFormat);
        makeTextFormat(L"Segoe UI Variable",14.f,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_TEXT_ALIGNMENT_CENTER,g.bodyCenterFormat);
        makeTextFormat(L"Segoe UI Variable",12.f,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING,g.smallFormat);
        makeTextFormat(L"Segoe UI Variable",12.f,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_TEXT_ALIGNMENT_CENTER,g.smallCenterFormat);
        makeTextFormat(L"Cascadia Mono",12.f,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_TEXT_ALIGNMENT_LEADING,g.monoFormat);
        if(g.monoFormat)g.monoFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(g.smallFormat)g.smallFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(g.bodyFormat)g.bodyFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(g.headingFormat)g.headingFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(g.titleFormat)g.titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    if(!g.target){
        RECT rc{};GetClientRect(g.hwnd,&rc);
        D2D1_RENDER_TARGET_PROPERTIES rp=D2D1::RenderTargetProperties();
        D2D1_HWND_RENDER_TARGET_PROPERTIES hp=D2D1::HwndRenderTargetProperties(g.hwnd,D2D1::SizeU(std::max(1L,rc.right),std::max(1L,rc.bottom)),D2D1_PRESENT_OPTIONS_NONE);
        if(FAILED(g.d2dFactory->CreateHwndRenderTarget(rp,hp,&g.target)))return false;
        g.target->SetDpi(g.dpi,g.dpi);rebuildBrushes();
    }
    return true;
}

void applyTheme(){
    rebuildPalette();
    if(g.hwnd){const BOOL dark=g.darkTheme?TRUE:FALSE;if(FAILED(DwmSetWindowAttribute(g.hwnd,20,&dark,sizeof(dark))))DwmSetWindowAttribute(g.hwnd,19,&dark,sizeof(dark));}
    rebuildBrushes();
    if(g.hwnd)InvalidateRect(g.hwnd,nullptr,FALSE);
}

bool contains(const D2D1_RECT_F&r,float x,float y){return x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom;}
bool intersects(const D2D1_RECT_F&a,const D2D1_RECT_F&b){return !(a.right<b.left||a.left>b.right||a.bottom<b.top||a.top>b.bottom);}

void addHit(D2D1_RECT_F r,HitKind kind,int id,int value=0,float lo=0,float hi=0){
    D2D1_RECT_F viewport=D2D1::RectF(kSidebarW,kTopH,g.target?g.target->GetSize().width:500.f,g.target?g.target->GetSize().height:500.f);
    if(kind==HitKind::Nav||intersects(r,viewport))g.hits.push_back({r,kind,id,value,lo,hi});
}

void drawText(const std::wstring&text,const D2D1_RECT_F&r,IDWriteTextFormat*fmt,ID2D1Brush*brush,D2D1_DRAW_TEXT_OPTIONS opt=D2D1_DRAW_TEXT_OPTIONS_CLIP){
    if(!g.target||!fmt||!brush||text.empty())return;g.target->DrawText(text.c_str(),(UINT32)text.size(),fmt,r,brush,opt,DWRITE_MEASURING_MODE_NATURAL);
}
void drawText(const wchar_t*text,const D2D1_RECT_F&r,IDWriteTextFormat*fmt,ID2D1Brush*brush){if(text)drawText(std::wstring(text),r,fmt,brush);}

void fillRounded(const D2D1_RECT_F&r,float radius,ID2D1Brush*brush){g.target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);}
void strokeRounded(const D2D1_RECT_F&r,float radius,ID2D1Brush*brush,float width=1.f){g.target->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush,width);}

D2D1_RECT_F card(float x,float y,float w,float h){D2D1_RECT_F r=D2D1::RectF(x,y,x+w,y+h);fillRounded(r,12.f,g.surfaceBrush.Get());strokeRounded(r,12.f,g.borderBrush.Get());return r;}

void sectionTitle(const wchar_t*title,const wchar_t*subtitle,float x,float&y,float w){
    drawText(title,D2D1::RectF(x,y,x+w,y+24),g.headingFormat.Get(),g.textBrush.Get());y+=25;
    if(subtitle&&*subtitle){drawText(subtitle,D2D1::RectF(x,y,x+w,y+38),g.smallFormat.Get(),g.mutedBrush.Get());y+=42;}else y+=10;
}

void button(const wchar_t*label,float x,float y,float w,float h,int id,bool accent=false,bool enabled=true){
    D2D1_RECT_F r=D2D1::RectF(x,y,x+w,y+h);bool hot=false;for(size_t i=0;i<g.hits.size();++i)if((int)i==g.hoverHit&&g.hits[i].kind==HitKind::Button&&g.hits[i].id==id)hot=true;
    ID2D1Brush*fill=accent?g.accentBrush.Get():(hot?g.accentSoftBrush.Get():g.surface2Brush.Get());fillRounded(r,9.f,fill);strokeRounded(r,9.f,accent?g.accentBrush.Get():g.borderBrush.Get());
    drawText(label,r,g.bodyCenterFormat.Get(),enabled?(accent?g.whiteBrush.Get():g.textBrush.Get()):g.mutedBrush.Get());if(enabled)addHit(r,HitKind::Button,id);
}

void toggle(const wchar_t*label,bool on,float x,float y,float w,int id,const wchar_t*note=nullptr){
    D2D1_RECT_F row=D2D1::RectF(x,y,x+w,y+42);drawText(label,D2D1::RectF(x,y,x+w-58,y+20),g.bodyFormat.Get(),g.textBrush.Get());
    if(note)drawText(note,D2D1::RectF(x,y+21,x+w-58,y+42),g.smallFormat.Get(),g.mutedBrush.Get());
    D2D1_RECT_F pill=D2D1::RectF(x+w-46,y+4,x+w,y+28);fillRounded(pill,12.f,on?g.accentBrush.Get():g.surface2Brush.Get());strokeRounded(pill,12.f,on?g.accentBrush.Get():g.borderBrush.Get());
    const float cx=on?pill.right-12.f:pill.left+12.f;g.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,(pill.top+pill.bottom)/2),8.f,8.f),on?g.whiteBrush.Get():g.mutedBrush.Get());
    addHit(row,HitKind::Toggle,id);
}

void choiceRow(const wchar_t*label,const std::vector<std::wstring>&items,int current,float x,float&y,float w,int choiceId,const wchar_t*note=nullptr){
    drawText(label,D2D1::RectF(x,y,x+w,y+20),g.bodyFormat.Get(),g.textBrush.Get());y+=26;
    const float gap=8.f;const float itemW=(w-gap*(items.size()-1))/std::max<size_t>(1,items.size());
    for(size_t i=0;i<items.size();++i){D2D1_RECT_F r=D2D1::RectF(x+i*(itemW+gap),y,x+i*(itemW+gap)+itemW,y+36);const bool sel=(int)i==current;fillRounded(r,8.f,sel?g.accentSoftBrush.Get():g.surface2Brush.Get());strokeRounded(r,8.f,sel?g.accentBrush.Get():g.borderBrush.Get());drawText(items[i],r,g.smallCenterFormat.Get(),sel?g.accentBrush.Get():g.textBrush.Get());addHit(r,HitKind::Choice,choiceId,(int)i);}y+=44;
    if(note){drawText(note,D2D1::RectF(x,y,x+w,y+38),g.smallFormat.Get(),g.mutedBrush.Get());y+=42;}
}

void choiceGrid(const wchar_t*label,const std::vector<std::wstring>&items,int current,int cols,float x,float&y,float w,int choiceId){
    drawText(label,D2D1::RectF(x,y,x+w,y+20),g.bodyFormat.Get(),g.textBrush.Get());y+=26;const float gap=8.f;const float itemW=(w-gap*(cols-1))/cols;
    for(size_t i=0;i<items.size();++i){int c=(int)i%cols,rw=(int)i/cols;float yy=y+rw*42;D2D1_RECT_F r=D2D1::RectF(x+c*(itemW+gap),yy,x+c*(itemW+gap)+itemW,yy+34);bool sel=(int)i==current;fillRounded(r,8.f,sel?g.accentSoftBrush.Get():g.surface2Brush.Get());strokeRounded(r,8.f,sel?g.accentBrush.Get():g.borderBrush.Get());drawText(items[i],r,g.smallCenterFormat.Get(),sel?g.accentBrush.Get():g.textBrush.Get());addHit(r,HitKind::Choice,choiceId,(int)i);}y+=((items.size()+cols-1)/cols)*42+8;
}

const SliderSpec* sliderSpec(SliderId id){for(auto&s:kSliderSpecs)if(s.id==id)return &s;return nullptr;}
float sliderValue(SliderId id){switch(id){
case SL_NR_INTENSITY:return g.settings.nrIntensity;case SL_NR_TONE:return g.settings.nrTone;case SL_NR_STRUCTURE:return g.settings.nrStructure;case SL_NR_SKIN:return g.settings.nrSkinStructure;
case SL_MOTION_SCALE:return g.settings.motionScale;case SL_CONFIDENCE:return g.settings.flowConfidenceThreshold;case SL_MOTION_X:return g.settings.motionScaleX;case SL_MOTION_Y:return g.settings.motionScaleY;case SL_DEADZONE:return g.settings.staticMotionDeadzone;case SL_DISOCC:return g.settings.disocclusionThreshold;case SL_RADIUS:return(float)g.settings.flowSearchRadius;
case SL_SHARPNESS:return g.settings.sharpness;case SL_EXPOSURE:return g.settings.exposure;case SL_TEMPORAL:return g.settings.temporalStrength;case SL_TEXT:return g.settings.textProtection;case SL_UI:return g.settings.uiProtection;case SL_MASK:return g.settings.controlMaskStrength;case SL_CLAMP:return g.settings.historyClamp;case SL_REACTIVE:return g.settings.reactiveStrength;case SL_EDGE:return g.settings.edgeThreshold;case SL_PAPER:return g.settings.nrPaperWhite;case SL_TRANSFER:return g.settings.nrTransferStrength;case SL_COLOR:return g.settings.nrColorStrength;case SL_DEBUG_SPLIT:return g.settings.debugSplit;default:return 0.f;}}
void setSliderValue(SliderId id,float v){const auto*sp=sliderSpec(id);if(!sp)return;v=std::clamp(v,sp->lo,sp->hi);switch(id){
case SL_NR_INTENSITY:g.settings.nrIntensity=v;break;case SL_NR_TONE:g.settings.nrTone=v;break;case SL_NR_STRUCTURE:g.settings.nrStructure=v;break;case SL_NR_SKIN:g.settings.nrSkinStructure=v;break;
case SL_MOTION_SCALE:g.settings.motionScale=v;break;case SL_CONFIDENCE:g.settings.flowConfidenceThreshold=v;break;case SL_MOTION_X:g.settings.motionScaleX=v;break;case SL_MOTION_Y:g.settings.motionScaleY=v;break;case SL_DEADZONE:g.settings.staticMotionDeadzone=v;break;case SL_DISOCC:g.settings.disocclusionThreshold=v;break;case SL_RADIUS:g.settings.flowSearchRadius=(std::uint32_t)std::lround(v);break;
case SL_SHARPNESS:g.settings.sharpness=v;break;case SL_EXPOSURE:g.settings.exposure=v;break;case SL_TEMPORAL:g.settings.temporalStrength=v;break;case SL_TEXT:g.settings.textProtection=v;break;case SL_UI:g.settings.uiProtection=v;break;case SL_MASK:g.settings.controlMaskStrength=v;break;case SL_CLAMP:g.settings.historyClamp=v;break;case SL_REACTIVE:g.settings.reactiveStrength=v;break;case SL_EDGE:g.settings.edgeThreshold=v;break;case SL_PAPER:g.settings.nrPaperWhite=v;break;case SL_TRANSFER:g.settings.nrTransferStrength=v;break;case SL_COLOR:g.settings.nrColorStrength=v;break;case SL_DEBUG_SPLIT:g.settings.debugSplit=v;break;default:break;}}

void slider(SliderId id,float x,float&y,float w){const auto*sp=sliderSpec(id);if(!sp)return;float v=sliderValue(id);std::wstringstream value;if(sp->precision==0)value<<(int)std::lround(v);else value<<std::fixed<<std::setprecision(sp->precision)<<v;
    drawText(sp->label,D2D1::RectF(x,y,x+w-70,y+20),g.bodyFormat.Get(),g.textBrush.Get());drawText(value.str(),D2D1::RectF(x+w-70,y,x+w,y+20),g.smallFormat.Get(),g.mutedBrush.Get());y+=28;
    D2D1_RECT_F track=D2D1::RectF(x,y+8,x+w,y+12);fillRounded(track,2.f,g.borderBrush.Get());float t=(v-sp->lo)/(sp->hi-sp->lo);float px=x+t*w;D2D1_RECT_F active=D2D1::RectF(x,y+8,px,y+12);if(active.right>active.left)fillRounded(active,2.f,g.accentBrush.Get());g.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,y+10),7.f,7.f),g.accentBrush.Get());D2D1_RECT_F hit=D2D1::RectF(x,y-4,x+w,y+25);addHit(hit,HitKind::Slider,(int)id,0,sp->lo,sp->hi);y+=34;
}

const wchar_t* archName(Arch a){switch(a){case Arch::X86:return L"x86";case Arch::X64:return L"x64";case Arch::Arm64:return L"ARM64";default:return L"unknown";}}
std::wstring rawDisplayName(const ProcessInfo&p){std::wstring n=p.name;if(n.size()>4&&_wcsicmp(n.c_str()+n.size()-4,L".exe")==0)n.resize(n.size()-4);if(!n.empty())n[0]=(wchar_t)std::towupper(n[0]);return n.empty()?L"Process":n;}

void saveProfileIfDirty(){if(!g.profileDirty||g.rootPath.empty())return;if(saveProfileFile(profilePath(g.rootPath),g.settings))g.profileDirty=false;}
void writeLive(bool persist=true){normalize(g.settings);g.shared.writeSettings(g.settings);if(persist&&!g.rootPath.empty())g.profileDirty=true;InvalidateRect(g.hwnd,nullptr,FALSE);}
void markCustomPreset(){g.presetIndex=5;}

PickerEntry* selectedEntry(){return g.selectedIndex>=0&&(size_t)g.selectedIndex<g.entries.size()?&g.entries[(size_t)g.selectedIndex]:nullptr;}
ProcessInfo* selected(){auto*e=selectedEntry();return e?&e->root:nullptr;}
std::wstring selectedApplicationName(){auto*e=selectedEntry();return e?e->displayName:L"(none)";}

std::wstring processDetail(const PickerEntry*e){if(!e)return L"No application selected.";std::wstringstream out;if(e->grouped){out<<e->processCount<<L" process"<<(e->processCount==1?L"":L"es");if(e->visibleWindowCount>1)out<<L" · "<<e->visibleWindowCount<<L" windows";}else out<<L"PID "<<e->root.pid;out<<L" · "<<archName(e->root.arch);out<<(e->anyDxgi||e->root.hasDxgi?L" · GPU/DXGI detected":L" · waiting for DXGI");if(e->root.blocksThirdPartyModules)out<<L" · signed-module policy (CIG)";return out.str();}

void setStatusText(const std::wstring&text){if(g.statusText!=text){g.statusText=text;InvalidateRect(g.hwnd,nullptr,FALSE);}}
const wchar_t* badgeText(BadgeState s){switch(s){case BadgeState::Active:return L"ACTIVE";case BadgeState::Passthrough:return L"PASSTHROUGH";case BadgeState::Error:return L"ERROR";default:return L"WAITING";}}
void setBadge(BadgeState s){if(g.badgeState!=s){g.badgeState=s;InvalidateRect(g.hwnd,nullptr,FALSE);}}
const wchar_t* graphicsApiText(GraphicsApi a){switch(a){case GraphicsApi::D3D11:return L"D3D11";case GraphicsApi::D3D12:return L"D3D12";default:return L"Unknown";}}
const wchar_t* neuralApiText(NeuralExecutionApi a){return a==NeuralExecutionApi::D3D12?L"D3D12":L"None";}
const wchar_t* runtimeStateText(RuntimeState s){switch(s){case RuntimeState::Injected:return L"Injected";case RuntimeState::Hooked:return L"Hooked";case RuntimeState::Processing:return L"Processing";case RuntimeState::Bypassed:return L"Passthrough";case RuntimeState::Error:return L"Error";case RuntimeState::Unloading:return L"Unloading";default:return L"Idle";}}
const wchar_t* debugViewText(DebugView v){switch(v){case DebugView::Original:return L"Original";case DebugView::Split:return L"Original / NR split";case DebugView::Difference:return L"Difference";case DebugView::Motion:return L"Motion vectors";case DebugView::MotionConfidence:return L"Motion confidence";case DebugView::ControlMask:return L"Control mask";case DebugView::Depth:return L"Depth guide";case DebugView::RawNeural:return L"Raw neural output";default:return L"Final output";}}
const wchar_t* pacingText(std::uint32_t m){return m==0?L"Synchronized":m==1?L"Adaptive":L"Asynchronous";}

std::wstring buildDiagnosticReport(const RuntimeStatus&s){std::wstringstream out;out<<L"UniversalDLSS5 diagnostics\r\n==========================\r\n";out<<L"Application: "<<selectedApplicationName()<<L"\r\nRoot PID: "<<g.rootPid<<L"\r\nBridge PID: "<<s.pid<<L"\r\nPrimary renderer PID: "<<(g.primaryRendererPid?g.primaryRendererPid:s.pid)<<L"\r\n";out<<L"State: "<<runtimeStateText(s.state)<<L"\r\nSource API: "<<graphicsApiText(s.api)<<L"\r\nNeural API: "<<neuralApiText(s.neuralApi)<<L"\r\nExecution location: "<<neuralExecutionLocationLabel(s.neuralLocation)<<L"\r\nNR backend kind: "<<neuralBackendKindLabel(s.neuralBackendKind)<<L"\r\nBackend: "<<(s.backendName[0]?s.backendName:L"(not initialized)")<<L"\r\nRuntime folder: "<<g.runtimePath<<L"\r\n";out<<L"Resolution: "<<s.width<<L" x "<<s.height<<L"\r\nFrames: presented="<<s.presentedFrames<<L", pipeline="<<s.processedFrames<<L", neural="<<s.neuralFrames<<L", bypassed="<<s.bypassedFrames<<L"\r\nNeural active this frame: "<<(s.neuralActive?L"YES":L"NO")<<L"\r\nEstimated FPS: "<<std::fixed<<std::setprecision(1)<<s.estimatedFps<<L"\r\nSubmit time: "<<std::fixed<<std::setprecision(2)<<s.lastGpuMs<<L" ms\r\n";if(s.queueCapacity)out<<L"Neural queue: "<<s.queueDepth<<L" / "<<s.queueCapacity<<L" active slots (limit "<<s.queueLimit<<L")\r\n";out<<L"Neural passes: requested="<<s.neuralPassesRequested<<L", executed="<<s.neuralPassesExecuted<<L"\r\nFrame pacing: "<<pacingText(s.framePacingMode)<<L"; neural output age="<<s.neuralOutputAgeFrames<<L" frame(s) / "<<std::fixed<<std::setprecision(1)<<s.neuralOutputAgeMs<<L" ms; wait="<<s.pacingWaitMs<<L" ms\r\n";if(s.schedulerBackpressureFrames||s.reusedNeuralFrames)out<<L"Scheduler: pressure="<<s.schedulerBackpressureFrames<<L", reused="<<s.reusedNeuralFrames<<L", reused this frame="<<(s.reusedNeuralOutput?L"YES":L"NO")<<L"\r\n";if(s.flowName[0])out<<L"Motion path: "<<s.flowName<<L"\r\n";if(s.nativeMotionCandidateId||s.nativeMotionCandidateScore)out<<L"Native motion candidate: id="<<s.nativeMotionCandidateId<<L", score="<<s.nativeMotionCandidateScore<<L"\r\n";out<<L"Camera matrices: current="<<(s.cameraCurrentValid?L"YES":L"NO")<<L", previous="<<(s.cameraPreviousValid?L"YES":L"NO")<<L", confidence="<<s.cameraConfidence<<L"%\r\nTemporal history: "<<(s.temporalHistoryValid?L"VALID":L"RESET/INVALID");if(s.temporalReason[0])out<<L" ("<<s.temporalReason<<L")";out<<L"\r\nDebug view: "<<debugViewText(g.settings.debugView);if(g.settings.debugView!=DebugView::Final)out<<L" (diagnostic visualization, not final output)";out<<L"\r\n";if(s.depthName[0])out<<L"Depth guide: "<<s.depthName<<L"\r\n";if(s.guideAdapterName[0])out<<L"Game guide adapter: "<<s.guideAdapterName<<L"\r\n";if(s.guideFields[0])out<<L"Guide fields: "<<s.guideFields<<L"\r\n";out<<L"Depth source active: "<<(s.gameDepthActive?L"GAME":L"SYNTHETIC")<<L"; convention="<<(s.depthInverted?L"INVERTED / reversed-Z":L"NORMAL Z")<<L"\r\nNR control mask: "<<(s.controlMaskActive?L"ON":L"OFF")<<L"; temporal reset this frame: "<<(s.temporalResetThisFrame?L"YES":L"NO")<<L"\r\n";const auto ft=formatRuntimeFeatureDiagnostics(s.feature);if(!ft.empty())out<<L"Feature versions: "<<ft<<L"\r\n";if(s.lastResult)out<<L"Last result: 0x"<<std::uppercase<<std::hex<<std::setw(8)<<std::setfill(L'0')<<(std::uint32_t)s.lastResult<<std::dec<<L"\r\n";if(s.failureStage!=PipelineStage::None)out<<L"Failure stage: "<<pipelineStageLabel(s.failureStage)<<L"\r\n";out<<L"\r\nPipeline stages\r\n---------------\r\n"<<formatPipelineStages(s.stageMask,s.failureStage)<<L"\r\n\r\nBackend message\r\n---------------\r\n"<<(s.message[0]?s.message:L"(none)")<<L"\r\n";return out.str();}

bool copyDiagnosticsToClipboard(){if(g.lastDiagnostic.empty()||!OpenClipboard(g.hwnd))return false;EmptyClipboard();SIZE_T bytes=(g.lastDiagnostic.size()+1)*sizeof(wchar_t);HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!mem){CloseClipboard();return false;}void*dst=GlobalLock(mem);if(!dst){GlobalFree(mem);CloseClipboard();return false;}memcpy(dst,g.lastDiagnostic.c_str(),bytes);GlobalUnlock(mem);if(!SetClipboardData(CF_UNICODETEXT,mem)){GlobalFree(mem);CloseClipboard();return false;}CloseClipboard();return true;}
std::string utf8(const std::wstring&t){if(t.empty())return{};int n=WideCharToMultiByte(CP_UTF8,0,t.data(),(int)t.size(),nullptr,0,nullptr,nullptr);std::string out((size_t)std::max(0,n),'\0');if(n>0)WideCharToMultiByte(CP_UTF8,0,t.data(),(int)t.size(),out.data(),n,nullptr,nullptr);return out;}
std::wstring saveDiagnosticsToFile(){if(g.lastDiagnostic.empty())return{};wchar_t local[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,_countof(local));fs::path dir=fs::path(n?std::wstring(local,n):g.appDir)/L"UniversalDLSS5"/L"logs";std::error_code ec;fs::create_directories(dir,ec);SYSTEMTIME t{};GetLocalTime(&t);wchar_t name[96]{};swprintf_s(name,L"diagnostics-%04u%02u%02u-%02u%02u%02u.txt",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);fs::path file=dir/name;std::ofstream f(file,std::ios::binary|std::ios::trunc);if(!f)return{};auto bytes=utf8(g.lastDiagnostic);f.write(bytes.data(),(std::streamsize)bytes.size());return f?file.wstring():std::wstring{};}

std::wstring validateRuntimeFolder(const std::wstring&folder){if(folder.empty())return L"Runtime folder is empty.";std::vector<std::wstring_view> missing;for(auto name:requiredDlssNrRuntimeFiles()){std::error_code ec;if(!fs::is_regular_file(fs::path(folder)/name,ec))missing.push_back(name);}return missing.empty()?L"DLSS-NR runtime found: nvngx_dlssnr.dll":formatMissingRuntimeFiles(missing);}
bool validateRuntimeForAttach(bool showOk){if(g.settings.backend==BackendMode::Passthrough)return true;auto msg=validateRuntimeFolder(g.runtimePath);if(msg.rfind(L"DLSS-NR runtime found",0)!=0){setStatusText(msg);return false;}if(showOk)setStatusText(msg);return true;}

void refresh(){DWORD keepPid=g.rootPid;std::wstring keepPath=g.rootPath;g.entries.clear();if(g.showAllProcesses){for(auto&p:enumerateProcesses()){if(!p.accessible||p.path.empty())continue;PickerEntry e{};e.root=std::move(p);e.displayName=rawDisplayName(e.root);e.anyDxgi=e.root.hasDxgi;e.grouped=false;g.entries.push_back(std::move(e));}}else{for(auto&app:enumerateApplications()){PickerEntry e{};e.root=std::move(app.root);e.displayName=std::move(app.displayName);e.processCount=app.processCount;e.visibleWindowCount=app.visibleWindowCount;e.anyDxgi=app.anyDxgi;e.grouped=true;g.entries.push_back(std::move(e));}}g.selectedIndex=-1;for(size_t i=0;i<g.entries.size();++i)if((!keepPath.empty()&&_wcsicmp(keepPath.c_str(),g.entries[i].root.path.c_str())==0)||(keepPath.empty()&&g.entries[i].root.pid==keepPid)){g.selectedIndex=(int)i;break;}if(g.selectedIndex<0&&!g.entries.empty())g.selectedIndex=0;InvalidateRect(g.hwnd,nullptr,FALSE);}

void selectChanged(int index){if(index<0||(size_t)index>=g.entries.size())return;saveProfileIfDirty();g.selectedIndex=index;auto*e=selectedEntry();g.rootPid=e->root.pid;g.rootPath=e->root.path;if(!g.rootPath.empty()){Settings s;g.settings=loadProfileFile(profilePath(g.rootPath),s)?s:defaultSettings();g.presetIndex=5;writeLive(false);applyTheme();}if(e->root.blocksThirdPartyModules)setStatusText(L"Selected application has a signed-module policy (CIG). Protected processes will be skipped; no mitigation bypass is attempted.");}

std::unordered_set<DWORD> injectedSnapshot(){std::scoped_lock lock(g.injectedMutex);return g.injected;}
void clearInjectedTracking(){std::scoped_lock lock(g.injectedMutex);g.injected.clear();}
void pruneInjected(){std::unordered_set<DWORD> alive;for(const auto&p:enumerateProcesses())alive.insert(p.pid);std::scoped_lock lock(g.injectedMutex);for(auto it=g.injected.begin();it!=g.injected.end();){if(!alive.contains(*it))it=g.injected.erase(it);else++it;}}
std::vector<ProcessInfo> attachTargetsFor(const ProcessInfo&root,bool followTree){if(!followTree)return{root};auto tree=processTree(root.pid);std::vector<ProcessInfo>out;out.reserve(tree.size());for(const auto&p:tree)if(p.pid==root.pid||p.hasDxgi)out.push_back(p);return out;}
struct AttachPassResult{int loaded{},failed{};std::wstring lastError;};
AttachPassResult injectTargetsOnce(const ProcessInfo&root,bool followTree,bool prune=true){AttachPassResult result{};if(prune)pruneInjected();for(const auto&target:attachTargetsFor(root,followTree)){bool reserved=false;{std::scoped_lock lock(g.injectedMutex);if(!g.injected.contains(target.pid)){g.injected.insert(target.pid);reserved=true;}}if(!reserved)continue;std::wstring message;if(injectBridge(target,g.appDir,message))++result.loaded;else{{std::scoped_lock lock(g.injectedMutex);g.injected.erase(target.pid);}++result.failed;result.lastError=target.name+L": "+message;}}return result;}
void configureMaintenance(const ProcessInfo&root,bool enabled){std::scoped_lock lock(g.maintenanceMutex);g.maintenanceRoot=root;g.maintenanceEnabled=enabled;g.maintenanceFollowTree=g.settings.attachProcessTree;}
void maintenanceLoop(std::stop_token stop){unsigned cycle=0;while(!stop.stop_requested()){ProcessInfo root{};bool enabled=false,follow=false;{std::scoped_lock lock(g.maintenanceMutex);root=g.maintenanceRoot;enabled=g.maintenanceEnabled;follow=g.maintenanceFollowTree;}if(enabled&&root.pid){injectTargetsOnce(root,follow,(cycle%4u)==0u);++cycle;}for(int i=0;i<40&&!stop.stop_requested();++i)Sleep(100);}}

void injectCurrentTree(){auto*p=selected();if(!p)return;if(!validateRuntimeForAttach(true))return;g.rootPid=p->pid;g.rootPath=p->path;g.shared.raw()->rootPid=g.rootPid;g.shared.setRuntimePath(g.runtimePath.c_str());g.shared.requestUnload(false);g.primaryRendererPid=0;g.shared.setPrimaryRendererPid(0);configureMaintenance(*p,true);auto r=injectTargetsOnce(*p,g.settings.attachProcessTree,true);setStatusText(L"Attach pass: "+std::to_wstring(r.loaded)+L" loaded, "+std::to_wstring(r.failed)+L" skipped/failed. "+r.lastError);}

RuntimeStatus electedStatus(const std::array<RuntimeStatus,32>&statuses,std::uint32_t pid){RuntimeStatus newest{};for(const auto&st:statuses){if(pid&&st.pid==pid)return st;if(!pid&&st.pid&&st.lastTickMs>=newest.lastTickMs)newest=st;}return newest;}
void poll(){saveProfileIfDirty();auto statuses=g.shared.readStatuses();auto now=GetTickCount64();std::array<RendererCandidate,32> candidates{};size_t count=0;auto injected=injectedSnapshot();for(const auto&st:statuses){if(!st.pid)continue;if(!injected.empty()&&!injected.contains(st.pid)&&st.pid!=g.rootPid)continue;candidates[count++]={st.pid,st.width,st.height,(std::uint32_t)st.api,st.estimatedFps,st.lastTickMs,st.pid==g.rootPid,st.neuralActive!=0,st.state==RuntimeState::Processing};}auto elected=electPrimaryRenderer(std::span<const RendererCandidate>(candidates.data(),count),g.primaryRendererPid,now);if(elected!=g.primaryRendererPid){auto old=g.primaryRendererPid;g.primaryRendererPid=elected;g.shared.setPrimaryRendererPid(elected);if(old&&elected&&old!=elected)g.shared.requestHistoryReset();}auto st=electedStatus(statuses,g.primaryRendererPid);if(!st.pid){setBadge(BadgeState::Waiting);return;}BadgeState visual=BadgeState::Waiting;if(st.neuralActive)visual=BadgeState::Active;else if(st.state==RuntimeState::Error||st.failureStage!=PipelineStage::None)visual=BadgeState::Error;else if(st.state==RuntimeState::Bypassed)visual=BadgeState::Passthrough;setBadge(visual);std::wstringstream summary;summary<<graphicsApiText(st.api)<<L" → "<<neuralApiText(st.neuralApi)<<L" · "<<st.width<<L"×"<<st.height<<L" · "<<std::fixed<<std::setprecision(1)<<st.estimatedFps<<L" FPS · "<<pacingText(st.framePacingMode);if(st.neuralPassesRequested)summary<<L" · "<<st.neuralPassesExecuted<<L"/"<<st.neuralPassesRequested<<L" pass";if(st.neuralOutputAgeFrames)summary<<L" · age "<<st.neuralOutputAgeFrames<<L"f";setStatusText(summary.str());auto diag=buildDiagnosticReport(st);bool changed=diag!=g.lastDiagnostic;g.lastRuntimeStatus=st;if(changed)g.lastDiagnostic=std::move(diag);if(changed)InvalidateRect(g.hwnd,nullptr,FALSE);}

std::wstring pickFolder(HWND owner){IFileOpenDialog*dialog=nullptr;std::wstring out;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);if(SUCCEEDED(dialog->Show(owner))){IShellItem*item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){out=path;CoTaskMemFree(path);}item->Release();}}dialog->Release();}return out;}

void applyPresetIndex(int index){if(index<0||index>=5)return;applyPreset(g.settings,(TuningPreset)index);g.presetIndex=index;writeLive();g.shared.requestHistoryReset();}

void setToggle(int id){switch(id){case TGL_SHOW_ALL:g.showAllProcesses=!g.showAllProcesses;refresh();if(g.selectedIndex>=0)selectChanged(g.selectedIndex);return;case TGL_ENABLE:g.settings.enabled=!g.settings.enabled;break;case TGL_UI_PROTECT:g.settings.protectUI=!g.settings.protectUI;break;case TGL_CTRL_MASK:g.settings.useControlMask=!g.settings.useControlMask;break;case TGL_AUTO_MASK:g.settings.nrAutoMask=!g.settings.nrAutoMask;break;case TGL_UI_CORRECTION:g.settings.nrUiCorrection=!g.settings.nrUiCorrection;break;case TGL_INVERT_Y:g.settings.invertMotionY=!g.settings.invertMotionY;break;case TGL_RESET_GAP:g.settings.resetOnTemporalGap=!g.settings.resetOnTemporalGap;break;case TGL_GAME_DEPTH:g.settings.useGameDepth=!g.settings.useGameDepth;break;case TGL_GAME_ADAPTER:g.settings.loadGameGuideAdapter=!g.settings.loadGameGuideAdapter;break;case TGL_TREE:g.settings.attachProcessTree=!g.settings.attachProcessTree;break;case TGL_ON12:g.settings.allowD3D11On12=!g.settings.allowD3D11On12;break;case TGL_SECONDARY:g.settings.processSecondarySwapchains=!g.settings.processSecondarySwapchains;break;case TGL_UNSUPPORTED:g.settings.attemptUnsupportedHardware=!g.settings.attemptUnsupportedHardware;break;default:return;}markCustomPreset();writeLive();}

void setChoice(int id,int value){switch(id){case CH_PRESET:applyPresetIndex(value);return;case CH_BACKEND:g.settings.backend=(BackendMode)std::clamp(value,0,2);break;case CH_PACING:g.settings.framePacing=(FramePacingMode)std::clamp(value,0,2);g.shared.requestHistoryReset();break;case CH_PASSES:g.settings.nrPasses=(std::uint32_t)std::clamp(value+1,1,4);break;case CH_STYLE:g.settings.nrStyle=(std::uint32_t)std::clamp(value,0,6);break;case CH_MODEL_PRESET:g.settings.nrPreset=(std::uint32_t)std::clamp(value,0,3);g.shared.requestHistoryReset();break;case CH_MOTION:g.settings.motionSource=(MotionSource)std::clamp(value,0,2);g.shared.requestHistoryReset();break;case CH_LATENCY:g.settings.latencyMode=(LatencyMode)std::clamp(value,0,2);break;case CH_DOWNSAMPLE:g.settings.flowDownsample=1u<<std::clamp(value,0,3);break;case CH_DEPTH:g.settings.depthMode=(DepthGuideMode)std::clamp(value,0,3);g.shared.requestHistoryReset();break;case CH_DEBUG:g.settings.debugView=(DebugView)std::clamp(value,0,8);break;case CH_THEME:g.settings.uiTheme=(UiTheme)std::clamp(value,0,2);writeLive();applyTheme();return;default:return;}markCustomPreset();writeLive();}

void handleButton(int id){switch(id){case BTN_REFRESH:refresh();if(g.selectedIndex>=0)selectChanged(g.selectedIndex);break;case BTN_ATTACH:injectCurrentTree();break;case BTN_DETACH:{std::scoped_lock lock(g.maintenanceMutex);g.maintenanceEnabled=false;g.maintenanceRoot={};}g.shared.requestUnload(true);g.shared.setPrimaryRendererPid(0);g.primaryRendererPid=0;clearInjectedTracking();setBadge(BadgeState::Waiting);setStatusText(L"Unload requested for injected bridges.");break;case BTN_RESET_HISTORY:g.shared.requestHistoryReset();setStatusText(L"Temporal history reset requested.");break;case BTN_BROWSE:{auto path=pickFolder(g.hwnd);if(!path.empty()){g.runtimePath=path;saveRuntimePathPreference(path);g.shared.setRuntimePath(path.c_str());setStatusText(validateRuntimeFolder(path));}}break;case BTN_CHECK_RUNTIME:setStatusText(validateRuntimeFolder(g.runtimePath));break;case BTN_IMPORT_NVIDIA:{auto sdk=pickFolder(g.hwnd);if(!sdk.empty()){setStatusText(L"Scanning the selected NVIDIA SDK for signed x64 Neural Rendering runtime files...");UpdateWindow(g.hwnd);auto result=importNvidiaRuntimeFromSdk(sdk,g.runtimePath);g.shared.setRuntimePath(g.runtimePath.c_str());setStatusText(result.summary);}}break;case BTN_OPEN_RUNTIME:{std::error_code ec;fs::create_directories(g.runtimePath,ec);ShellExecuteW(g.hwnd,L"open",g.runtimePath.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}case BTN_NVIDIA_DOWNLOAD:ShellExecuteW(g.hwnd,L"open",L"https://developer.nvidia.com/rtx/streamline/get-started",nullptr,nullptr,SW_SHOWNORMAL);break;case BTN_RETRY_NEURAL:g.shared.requestNeuralRetry();setBadge(BadgeState::Waiting);setStatusText(L"Neural backend retry requested.");break;case BTN_COPY_DIAG:setStatusText(copyDiagnosticsToClipboard()?L"Diagnostics copied to clipboard.":L"No diagnostics available to copy.");break;case BTN_SAVE_DIAG:{auto p=saveDiagnosticsToFile();setStatusText(p.empty()?L"Could not save diagnostics.":L"Saved diagnostics: "+p);}break;}}

void drawBadge(float x,float y){const wchar_t*txt=badgeText(g.badgeState);ID2D1Brush*b=g.mutedBrush.Get();if(g.badgeState==BadgeState::Active)b=g.goodBrush.Get();else if(g.badgeState==BadgeState::Passthrough)b=g.warnBrush.Get();else if(g.badgeState==BadgeState::Error)b=g.badBrush.Get();D2D1_RECT_F r=D2D1::RectF(x,y,x+112,y+30);fillRounded(r,15.f,b);drawText(txt,r,g.smallCenterFormat.Get(),g.whiteBrush.Get());}

void drawHeader(float w){g.target->FillRectangle(D2D1::RectF(0,0,w,kTopH),g.surfaceBrush.Get());g.target->DrawLine(D2D1::Point2F(0,kTopH-.5f),D2D1::Point2F(w,kTopH-.5f),g.borderBrush.Get());drawText(L"Universal DLSS 5",D2D1::RectF(24,16,300,45),g.titleFormat.Get(),g.textBrush.Get());std::wstring sub=selectedApplicationName();if(g.lastRuntimeStatus.width)sub+=L" · "+std::wstring(graphicsApiText(g.lastRuntimeStatus.api))+L" · "+std::to_wstring(g.lastRuntimeStatus.width)+L"×"+std::to_wstring(g.lastRuntimeStatus.height);drawText(sub,D2D1::RectF(25,47,w-190,68),g.smallFormat.Get(),g.mutedBrush.Get());drawBadge(w-142,22);}

void drawSidebar(float h){g.target->FillRectangle(D2D1::RectF(0,kTopH,kSidebarW,h),g.surfaceBrush.Get());g.target->DrawLine(D2D1::Point2F(kSidebarW-.5f,kTopH),D2D1::Point2F(kSidebarW-.5f,h),g.borderBrush.Get());static const wchar_t*names[]={L"Application",L"Neural Rendering",L"Motion",L"Composition",L"Diagnostics",L"Advanced"};for(int i=0;i<6;++i){float y=kTopH+20+i*48;D2D1_RECT_F r=D2D1::RectF(14,y,kSidebarW-14,y+38);bool sel=(int)g.page==i;if(sel)fillRounded(r,9.f,g.accentSoftBrush.Get());drawText(names[i],D2D1::RectF(28,y,170,y+38),g.bodyFormat.Get(),sel?g.accentBrush.Get():g.textBrush.Get());addHit(r,HitKind::Nav,i);}drawText(L"v0.2.8 experimental",D2D1::RectF(20,h-40,kSidebarW-16,h-18),g.smallFormat.Get(),g.mutedBrush.Get());}

void beginPage(const wchar_t*title,const wchar_t*subtitle,float contentX,float&y,float contentW){drawText(title,D2D1::RectF(contentX,y,contentX+contentW,y+30),g.titleFormat.Get(),g.textBrush.Get());y+=34;drawText(subtitle,D2D1::RectF(contentX,y,contentX+contentW,y+38),g.smallFormat.Get(),g.mutedBrush.Get());y+=50;}

void drawApplicationPage(float x,float&y,float w){beginPage(L"Application",L"Select a renderer process, validate the runtime, then attach the bridge.",x,y,w);
    float listH=std::min(330.f,std::max(190.f,54.f*(float)std::min<size_t>(g.entries.size(),6)+70.f));auto c=card(x,y,w,listH);float cx=x+18,cy=y+16,cw=w-36;drawText(L"Target application",D2D1::RectF(cx,cy,cx+260,cy+24),g.headingFormat.Get(),g.textBrush.Get());button(L"Refresh",x+w-194,cy-2,82,32,BTN_REFRESH);toggle(L"Show all",g.showAllProcesses,x+w-100,cy-4,82,TGL_SHOW_ALL);cy+=42;
    if(g.entries.empty())drawText(L"No attachable applications found.",D2D1::RectF(cx,cy,cx+cw,cy+40),g.bodyFormat.Get(),g.mutedBrush.Get());
    else for(size_t i=0;i<g.entries.size()&&i<8;++i){auto&e=g.entries[i];D2D1_RECT_F row=D2D1::RectF(cx,cy,cx+cw,cy+44);bool sel=(int)i==g.selectedIndex;if(sel)fillRounded(row,8.f,g.accentSoftBrush.Get());drawText(e.displayName,D2D1::RectF(cx+12,cy+5,cx+cw-190,cy+24),g.bodyFormat.Get(),sel?g.accentBrush.Get():g.textBrush.Get());drawText(processDetail(&e),D2D1::RectF(cx+12,cy+24,cx+cw-12,cy+42),g.smallFormat.Get(),g.mutedBrush.Get());addHit(row,HitKind::AppRow,0,(int)i);cy+=48;if(cy>c.bottom-48)break;}y+=listH+14;
    float runtimeH=366;card(x,y,w,runtimeH);cy=y+18;drawText(L"NVIDIA runtime & attachment",D2D1::RectF(x+18,cy,x+w-18,cy+24),g.headingFormat.Get(),g.textBrush.Get());cy+=38;drawText(L"Runtime destination",D2D1::RectF(x+18,cy,x+160,cy+20),g.smallFormat.Get(),g.mutedBrush.Get());drawText(g.runtimePath,D2D1::RectF(x+18,cy+20,x+w-292,cy+45),g.bodyFormat.Get(),g.textBrush.Get());button(L"Choose folder",x+w-276,cy+8,104,34,BTN_BROWSE);button(L"Open runtime",x+w-162,cy+8,104,34,BTN_OPEN_RUNTIME);cy+=62;
    drawText(L"UniversalDLSS5 does not redistribute NVIDIA's proprietary DLSS runtime. Download/extract the official NVIDIA SDK, then import only the NVIDIA Neural Rendering DLLs into this runtime folder.",D2D1::RectF(x+18,cy,x+w-18,cy+44),g.smallFormat.Get(),g.mutedBrush.Get());cy+=52;button(L"Import NVIDIA SDK...",x+18,cy,170,36,BTN_IMPORT_NVIDIA,true);button(L"NVIDIA download",x+198,cy,132,36,BTN_NVIDIA_DOWNLOAD);button(L"Check runtime",x+340,cy,118,36,BTN_CHECK_RUNTIME);cy+=52;
    std::vector<std::wstring> backends={L"Direct in-game",L"Passthrough",L"External host"};choiceRow(L"Backend",backends,(int)g.settings.backend,x+18,cy,w-36,CH_BACKEND);button(L"Attach",x+18,cy,92,38,BTN_ATTACH,true);button(L"Detach",x+120,cy,92,38,BTN_DETACH);button(L"Reset history",x+222,cy,116,38,BTN_RESET_HISTORY);button(L"Retry neural",x+348,cy,112,38,BTN_RETRY_NEURAL);y+=runtimeH+14;
    card(x,y,w,78);drawText(L"Status",D2D1::RectF(x+18,y+14,x+90,y+34),g.smallFormat.Get(),g.mutedBrush.Get());drawText(g.statusText,D2D1::RectF(x+18,y+35,x+w-18,y+70),g.bodyFormat.Get(),g.textBrush.Get());y+=92;
    card(x,y,w,96);drawText(L"Security notice",D2D1::RectF(x+18,y+14,x+w-18,y+36),g.headingFormat.Get(),g.warnBrush.Get());drawText(L"Some antivirus products may flag DLL injection or graphics API hooking. Verify the GitHub release hash/source and investigate any warning; do not disable security software just to run UniversalDLSS5.",D2D1::RectF(x+18,y+42,x+w-18,y+88),g.smallFormat.Get(),g.mutedBrush.Get());y+=110;
}

void drawNeuralPage(float x,float&y,float w){beginPage(L"Neural Rendering",L"Feature-18 controls, pass count and presentation pacing.",x,y,w);float cy;
    card(x,y,w,198);cy=y+18;sectionTitle(L"Pipeline",L"Synchronized pacing is the default: FPS falls to neural throughput instead of replaying stale frames.",x+18,cy,w-36);toggle(L"Processing enabled",g.settings.enabled,x+18,cy,w-36,TGL_ENABLE);cy+=44;toggle(L"Protect UI / text",g.settings.protectUI,x+18,cy,w-36,TGL_UI_PROTECT);y+=212;
    float h=278;card(x,y,w,h);cy=y+18;std::vector<std::wstring> pacing={L"Synchronized",L"Adaptive",L"Asynchronous"};choiceRow(L"Frame pacing",pacing,(int)g.settings.framePacing,x+18,cy,w-36,CH_PACING,L"Adaptive permits at most one-frame-old output. Asynchronous maximizes throughput but can trail under load.");std::vector<std::wstring>passes={L"1×",L"2×",L"3×",L"4×"};choiceRow(L"Neural passes",passes,(int)g.settings.nrPasses-1,x+18,cy,w-36,CH_PASSES,L"Passes 2–4 are same-frame reset-only refinement passes and remain one atomic frame job.");y+=h+14;
    h=372;card(x,y,w,h);cy=y+18;std::vector<std::wstring>presets={L"Default",L"Desktop",L"2D game",L"Video",L"Aggressive"};choiceRow(L"Tuning preset",presets,std::min(g.presetIndex,4),x+18,cy,w-36,CH_PRESET);slider(SL_NR_INTENSITY,x+18,cy,w-36);slider(SL_NR_TONE,x+18,cy,w-36);slider(SL_NR_STRUCTURE,x+18,cy,w-36);slider(SL_NR_SKIN,x+18,cy,w-36);y+=h+14;
    h=238;card(x,y,w,h);cy=y+18;std::vector<std::wstring>styles={L"Default",L"Natural",L"Cinematic",L"3",L"4",L"5",L"6"};choiceRow(L"Style",styles,(int)g.settings.nrStyle,x+18,cy,w-36,CH_STYLE);std::vector<std::wstring>mp={L"0",L"1",L"2",L"3"};choiceRow(L"Model preset",mp,(int)g.settings.nrPreset,x+18,cy,w-36,CH_MODEL_PRESET);toggle(L"Semantic auto-mask",g.settings.nrAutoMask,x+18,cy,(w-46)/2,TGL_AUTO_MASK);toggle(L"UI correction",g.settings.nrUiCorrection,x+w/2+6,cy,(w-46)/2,TGL_UI_CORRECTION);y+=h+14;
}

void drawMotionPage(float x,float&y,float w){beginPage(L"Motion & Temporal Guides",L"Guide quality determines whether temporal history is safe to retain.",x,y,w);float cy;float h=244;card(x,y,w,h);cy=y+18;std::vector<std::wstring>motion={L"Auto",L"Optical flow",L"Zero"};choiceRow(L"Motion source",motion,(int)g.settings.motionSource,x+18,cy,w-36,CH_MOTION,L"Auto prefers native game motion, then camera+depth, NVOFA, and finally safe zero motion.");std::vector<std::wstring>lat={L"Ultra low",L"Balanced",L"Quality"};choiceRow(L"Flow quality",lat,(int)g.settings.latencyMode,x+18,cy,w-36,CH_LATENCY);y+=h+14;
    h=490;card(x,y,w,h);cy=y+18;slider(SL_MOTION_SCALE,x+18,cy,w-36);slider(SL_CONFIDENCE,x+18,cy,w-36);slider(SL_MOTION_X,x+18,cy,w-36);slider(SL_MOTION_Y,x+18,cy,w-36);slider(SL_DEADZONE,x+18,cy,w-36);slider(SL_DISOCC,x+18,cy,w-36);slider(SL_RADIUS,x+18,cy,w-36);y+=h+14;
    h=322;card(x,y,w,h);cy=y+18;std::vector<std::wstring>ds={L"1× full",L"2×",L"4×",L"8× fast"};int d=0;for(auto q=g.settings.flowDownsample;q>1;q>>=1)++d;choiceRow(L"Flow resolution",ds,d,x+18,cy,w-36,CH_DOWNSAMPLE);std::vector<std::wstring>depth={L"Auto/game",L"Synthetic",L"Normal Z",L"Reversed Z"};choiceRow(L"Depth guide",depth,(int)g.settings.depthMode,x+18,cy,w-36,CH_DEPTH);toggle(L"Invert motion Y",g.settings.invertMotionY,x+18,cy,w-36,TGL_INVERT_Y);cy+=44;toggle(L"Reset after temporal gap",g.settings.resetOnTemporalGap,x+18,cy,w-36,TGL_RESET_GAP);cy+=44;toggle(L"Use game depth",g.settings.useGameDepth,x+18,cy,w-36,TGL_GAME_DEPTH);y+=h+14;
}

void drawCompositionPage(float x,float&y,float w){beginPage(L"Composition",L"Final blend, edge protection and post-composite controls.",x,y,w);float cy;float h=800;card(x,y,w,h);cy=y+18;slider(SL_SHARPNESS,x+18,cy,w-36);slider(SL_EXPOSURE,x+18,cy,w-36);slider(SL_TEMPORAL,x+18,cy,w-36);slider(SL_TEXT,x+18,cy,w-36);slider(SL_UI,x+18,cy,w-36);slider(SL_MASK,x+18,cy,w-36);slider(SL_CLAMP,x+18,cy,w-36);slider(SL_REACTIVE,x+18,cy,w-36);slider(SL_EDGE,x+18,cy,w-36);slider(SL_PAPER,x+18,cy,w-36);slider(SL_TRANSFER,x+18,cy,w-36);slider(SL_COLOR,x+18,cy,w-36);y+=h+14;}

void metricCard(const wchar_t*label,const std::wstring&value,float x,float y,float w){card(x,y,w,78);drawText(label,D2D1::RectF(x+14,y+12,x+w-14,y+32),g.smallFormat.Get(),g.mutedBrush.Get());drawText(value,D2D1::RectF(x+14,y+35,x+w-14,y+66),g.headingFormat.Get(),g.textBrush.Get());}

void drawDiagnosticsPage(float x,float&y,float w){beginPage(L"Diagnostics",L"Live renderer ownership, frame pacing and backend state.",x,y,w);auto&s=g.lastRuntimeStatus;float gap=10,metricW=(w-gap*3)/4;std::wstringstream a,b,c,d;a<<std::fixed<<std::setprecision(1)<<s.estimatedFps<<L" FPS";b<<s.neuralOutputAgeFrames<<L"f / "<<std::fixed<<std::setprecision(1)<<s.neuralOutputAgeMs<<L" ms";c<<std::fixed<<std::setprecision(1)<<s.pacingWaitMs<<L" ms";d<<s.queueDepth<<L" / "<<s.queueCapacity;metricCard(L"Game / Present",a.str(),x,y,metricW);metricCard(L"Neural output age",b.str(),x+metricW+gap,y,metricW);metricCard(L"Pacing wait",c.str(),x+(metricW+gap)*2,y,metricW);metricCard(L"Neural queue",d.str(),x+(metricW+gap)*3,y,metricW);y+=92;button(L"Copy diagnostics",x,y,132,36,BTN_COPY_DIAG);button(L"Save diagnostics",x+142,y,132,36,BTN_SAVE_DIAG);y+=50;float lines=1.f+(float)std::count(g.lastDiagnostic.begin(),g.lastDiagnostic.end(),L'\n');float textH=std::max(420.f,lines*17.f+28.f);card(x,y,w,textH);drawText(g.lastDiagnostic,D2D1::RectF(x+16,y+14,x+w-16,y+textH-14),g.monoFormat.Get(),g.textBrush.Get());y+=textH+14;}

void drawAdvancedPage(float x,float&y,float w){beginPage(L"Advanced",L"Compatibility, diagnostics view and controller appearance.",x,y,w);float cy;float h=280;card(x,y,w,h);cy=y+18;toggle(L"Follow application process tree",g.settings.attachProcessTree,x+18,cy,w-36,TGL_TREE,L"Recommended for launchers and multi-process games.");cy+=48;toggle(L"Enable D3D11On12 source bridge",g.settings.allowD3D11On12,x+18,cy,w-36,TGL_ON12);cy+=48;toggle(L"Process secondary swapchains",g.settings.processSecondarySwapchains,x+18,cy,w-36,TGL_SECONDARY);cy+=48;toggle(L"Attempt unsupported hardware",g.settings.attemptUnsupportedHardware,x+18,cy,w-36,TGL_UNSUPPORTED);cy+=48;toggle(L"Load GameGuides adapter",g.settings.loadGameGuideAdapter,x+18,cy,w-36,TGL_GAME_ADAPTER);y+=h+14;
    h=178;card(x,y,w,h);cy=y+18;std::vector<std::wstring>themes={L"System",L"Light",L"Dark"};choiceRow(L"Appearance",themes,(int)g.settings.uiTheme,x+18,cy,w-36,CH_THEME,L"The canvas, title bar and text all update together; no legacy child controls are used.");y+=h+14;
    h=278;card(x,y,w,h);cy=y+18;std::vector<std::wstring>debug={L"Final",L"Original",L"Split",L"Difference",L"Motion",L"Confidence",L"Mask",L"Depth",L"Raw neural"};choiceGrid(L"Debug view",debug,(int)g.settings.debugView,3,x+18,cy,w-36,CH_DEBUG);slider(SL_DEBUG_SPLIT,x+18,cy,w-36);y+=h+14;}

void drawContent(){D2D1_SIZE_F sz=g.target->GetSize();D2D1_RECT_F viewport=D2D1::RectF(kSidebarW,kTopH,sz.width,sz.height);g.target->PushAxisAlignedClip(viewport,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);float x=kSidebarW+kMargin;float w=std::max(300.f,sz.width-x-kMargin);float y=kTopH+kMargin-g.contentScroll;switch(g.page){case Page::Application:drawApplicationPage(x,y,w);break;case Page::Neural:drawNeuralPage(x,y,w);break;case Page::Motion:drawMotionPage(x,y,w);break;case Page::Composition:drawCompositionPage(x,y,w);break;case Page::Diagnostics:drawDiagnosticsPage(x,y,w);break;case Page::Advanced:drawAdvancedPage(x,y,w);break;}g.contentMaxHeight=std::max(0.f,y+g.contentScroll-kTopH);g.contentViewportHeight=std::max(1.f,sz.height-kTopH);g.target->PopAxisAlignedClip();}

void paint(){if(!ensureGraphics())return;g.hits.clear();g.target->BeginDraw();g.target->Clear(g.palette.background);D2D1_SIZE_F sz=g.target->GetSize();drawHeader(sz.width);drawSidebar(sz.height);drawContent();HRESULT hr=g.target->EndDraw();if(hr==D2DERR_RECREATE_TARGET){g.target.Reset();releaseBrushes();}}

int hitAt(float x,float y){for(int i=(int)g.hits.size()-1;i>=0;--i)if(contains(g.hits[(size_t)i].rect,x,y))return i;return-1;}
void clampScroll(){float maxScroll=std::max(0.f,g.contentMaxHeight-g.contentViewportHeight+18.f);g.contentScroll=std::clamp(g.contentScroll,0.f,maxScroll);}

void updateActiveSlider(float x){if(!g.activeSlider)return;float t=(x-g.activeSliderRect.left)/std::max(1.f,g.activeSliderRect.right-g.activeSliderRect.left);float v=g.activeSliderLo+std::clamp(t,0.f,1.f)*(g.activeSliderHi-g.activeSliderLo);setSliderValue((SliderId)g.activeSlider,v);normalize(g.settings);markCustomPreset();InvalidateRect(g.hwnd,nullptr,FALSE);}

void handleHit(const Hit&h,float x){switch(h.kind){case HitKind::Nav:g.page=(Page)h.id;g.contentScroll=0;InvalidateRect(g.hwnd,nullptr,FALSE);break;case HitKind::Button:handleButton(h.id);break;case HitKind::Toggle:setToggle(h.id);break;case HitKind::Choice:setChoice(h.id,h.value);break;case HitKind::AppRow:selectChanged(h.value);break;case HitKind::Slider:g.activeSlider=h.id;g.activeSliderRect=h.rect;g.activeSliderLo=h.lo;g.activeSliderHi=h.hi;SetCapture(g.hwnd);updateActiveSlider(x);break;}}

LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){
case WM_CREATE:g.hwnd=h;g.dpi=(float)GetDpiForWindow(h);rebuildPalette();applyTheme();refresh();if(g.selectedIndex>=0)selectChanged(g.selectedIndex);SetTimer(h,1,500,nullptr);g.maintenanceThread=std::jthread(maintenanceLoop);return 0;
case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(h,&ps);paint();EndPaint(h,&ps);return 0;}
case WM_ERASEBKGND:return 1;
case WM_SIZE:if(g.target){UINT ww=LOWORD(l),hh=HIWORD(l);if(ww&&hh)g.target->Resize(D2D1::SizeU(ww,hh));}clampScroll();InvalidateRect(h,nullptr,FALSE);return 0;
case WM_DPICHANGED:{g.dpi=(float)HIWORD(w);RECT*pr=(RECT*)l;SetWindowPos(h,nullptr,pr->left,pr->top,pr->right-pr->left,pr->bottom-pr->top,SWP_NOZORDER|SWP_NOACTIVATE);if(g.target)g.target->SetDpi(g.dpi,g.dpi);InvalidateRect(h,nullptr,FALSE);return 0;}
case WM_GETMINMAXINFO:{auto*i=(MINMAXINFO*)l;i->ptMinTrackSize.x=980;i->ptMinTrackSize.y=680;return 0;}
case WM_MOUSEMOVE:{float scale=96.f/g.dpi;float x=(float)GET_X_LPARAM(l)*scale,y=(float)GET_Y_LPARAM(l)*scale;if(g.activeSlider){updateActiveSlider(x);return 0;}int hit=hitAt(x,y);if(hit!=g.hoverHit){g.hoverHit=hit;InvalidateRect(h,nullptr,FALSE);}return 0;}
case WM_LBUTTONDOWN:{float scale=96.f/g.dpi;float x=(float)GET_X_LPARAM(l)*scale,y=(float)GET_Y_LPARAM(l)*scale;int hit=hitAt(x,y);if(hit>=0)handleHit(g.hits[(size_t)hit],x);return 0;}
case WM_LBUTTONUP:if(g.activeSlider){ReleaseCapture();g.activeSlider=0;writeLive();}return 0;
case WM_MOUSEWHEEL:{float step=(GET_WHEEL_DELTA_WPARAM(w)/120.f)*72.f;g.contentScroll-=step;clampScroll();InvalidateRect(h,nullptr,FALSE);return 0;}
case WM_TIMER:poll();return 0;
case WM_SETTINGCHANGE:if(g.settings.uiTheme==UiTheme::System)applyTheme();return 0;
case WM_DESTROY:KillTimer(h,1);{std::scoped_lock lock(g.maintenanceMutex);g.maintenanceEnabled=false;}if(g.maintenanceThread.joinable()){g.maintenanceThread.request_stop();g.maintenanceThread.join();}saveProfileIfDirty();g.shared.requestUnload(true);g.shared.setPrimaryRendererPid(0);g.target.Reset();releaseBrushes();g.d2dFactory.Reset();g.dwriteFactory.Reset();PostQuitMessage(0);return 0;
}return DefWindowProcW(h,m,w,l);}

} // namespace

int runUi(HINSTANCE instance,int show){g.inst=instance;g.appDir=applicationDirectory();g.runtimePath=loadRuntimePathPreference(defaultRuntimeDirectory());SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(!g.shared.create()){if(SUCCEEDED(co))CoUninitialize();return 2;}WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));wc.hIconSm=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,16,16,LR_DEFAULTCOLOR);wc.hbrBackground=nullptr;wc.lpszClassName=L"UniversalDLSS5.Controller";RegisterClassExW(&wc);HWND h=CreateWindowExW(0,wc.lpszClassName,L"Universal DLSS 5 - Neural Rendering Controller",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1240,820,nullptr,nullptr,instance,nullptr);if(!h){if(SUCCEEDED(co))CoUninitialize();return 3;}ShowWindow(h,show);UpdateWindow(h);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(SUCCEEDED(co))CoUninitialize();return(int)msg.wParam;}

} // namespace udlss::controller
