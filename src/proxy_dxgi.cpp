// proxy_dxgi.cpp — makes GlyphSwap loadable as a drop-in **dxgi.dll** wrapper.
//
// Placed next to OPWS.exe, the game loads this instead of the system dxgi.dll
// (the app directory is searched first, and dxgi is not a KnownDLL). That means
// NO injector and NO CreateRemoteThread — so antivirus has nothing to flag.
//
// Every export of the real dxgi.dll is re-exported here as a tiny signature-
// agnostic tail-jump thunk that forwards to the REAL system dxgi.dll. Our
// DllMain resolves those real addresses first, then starts the mod.
//
// Compiled only when GLYPHSWAP_AS_PROXY is defined (see build.ps1); the export
// names + ordinals come from src/dxgi.def.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

extern "C" void GlyphSwap_Start();   // defined in dllmain.cpp

// Real dxgi function pointers (filled in DllMain, before the game calls them).
extern "C" {
void* p_ApplyCompatResolutionQuirking   = nullptr;
void* p_CompatString                    = nullptr;
void* p_CompatValue                     = nullptr;
void* p_CreateDXGIFactory               = nullptr;
void* p_CreateDXGIFactory1              = nullptr;
void* p_CreateDXGIFactory2              = nullptr;
void* p_DXGID3D10CreateDevice           = nullptr;
void* p_DXGID3D10CreateLayeredDevice    = nullptr;
void* p_DXGID3D10GetLayeredDeviceSize   = nullptr;
void* p_DXGID3D10RegisterLayers         = nullptr;
void* p_DXGIDeclareAdapterRemovalSupport= nullptr;
void* p_DXGIDumpJournal                 = nullptr;
void* p_DXGIGetDebugInterface1          = nullptr;
void* p_DXGIReportAdapterConfiguration  = nullptr;
void* p_PIXBeginCapture                 = nullptr;
void* p_PIXEndCapture                   = nullptr;
void* p_PIXGetCaptureState              = nullptr;
void* p_SetAppCompatStringPointer       = nullptr;
void* p_UpdateHMDEmulationStatus        = nullptr;
}

// One naked tail-jump per export (x64). No prologue: the caller's arguments stay
// exactly where they are, we jump to the real function, and it returns straight
// to the original caller. Works for any signature / calling convention.
asm(
".text\n"
".globl ApplyCompatResolutionQuirking\n ApplyCompatResolutionQuirking:    jmp *p_ApplyCompatResolutionQuirking(%rip)\n"
".globl CompatString\n CompatString:                                      jmp *p_CompatString(%rip)\n"
".globl CompatValue\n CompatValue:                                        jmp *p_CompatValue(%rip)\n"
".globl CreateDXGIFactory\n CreateDXGIFactory:                            jmp *p_CreateDXGIFactory(%rip)\n"
".globl CreateDXGIFactory1\n CreateDXGIFactory1:                          jmp *p_CreateDXGIFactory1(%rip)\n"
".globl CreateDXGIFactory2\n CreateDXGIFactory2:                          jmp *p_CreateDXGIFactory2(%rip)\n"
".globl DXGID3D10CreateDevice\n DXGID3D10CreateDevice:                    jmp *p_DXGID3D10CreateDevice(%rip)\n"
".globl DXGID3D10CreateLayeredDevice\n DXGID3D10CreateLayeredDevice:      jmp *p_DXGID3D10CreateLayeredDevice(%rip)\n"
".globl DXGID3D10GetLayeredDeviceSize\n DXGID3D10GetLayeredDeviceSize:    jmp *p_DXGID3D10GetLayeredDeviceSize(%rip)\n"
".globl DXGID3D10RegisterLayers\n DXGID3D10RegisterLayers:                jmp *p_DXGID3D10RegisterLayers(%rip)\n"
".globl DXGIDeclareAdapterRemovalSupport\n DXGIDeclareAdapterRemovalSupport: jmp *p_DXGIDeclareAdapterRemovalSupport(%rip)\n"
".globl DXGIDumpJournal\n DXGIDumpJournal:                                jmp *p_DXGIDumpJournal(%rip)\n"
".globl DXGIGetDebugInterface1\n DXGIGetDebugInterface1:                  jmp *p_DXGIGetDebugInterface1(%rip)\n"
".globl DXGIReportAdapterConfiguration\n DXGIReportAdapterConfiguration:  jmp *p_DXGIReportAdapterConfiguration(%rip)\n"
".globl PIXBeginCapture\n PIXBeginCapture:                                jmp *p_PIXBeginCapture(%rip)\n"
".globl PIXEndCapture\n PIXEndCapture:                                    jmp *p_PIXEndCapture(%rip)\n"
".globl PIXGetCaptureState\n PIXGetCaptureState:                          jmp *p_PIXGetCaptureState(%rip)\n"
".globl SetAppCompatStringPointer\n SetAppCompatStringPointer:            jmp *p_SetAppCompatStringPointer(%rip)\n"
".globl UpdateHMDEmulationStatus\n UpdateHMDEmulationStatus:              jmp *p_UpdateHMDEmulationStatus(%rip)\n"
);

static const char* const kNames[] = {
    "ApplyCompatResolutionQuirking","CompatString","CompatValue","CreateDXGIFactory",
    "CreateDXGIFactory1","CreateDXGIFactory2","DXGID3D10CreateDevice","DXGID3D10CreateLayeredDevice",
    "DXGID3D10GetLayeredDeviceSize","DXGID3D10RegisterLayers","DXGIDeclareAdapterRemovalSupport",
    "DXGIDumpJournal","DXGIGetDebugInterface1","DXGIReportAdapterConfiguration","PIXBeginCapture",
    "PIXEndCapture","PIXGetCaptureState","SetAppCompatStringPointer","UpdateHMDEmulationStatus"
};
static void** const kPtrs[] = {
    &p_ApplyCompatResolutionQuirking,&p_CompatString,&p_CompatValue,&p_CreateDXGIFactory,
    &p_CreateDXGIFactory1,&p_CreateDXGIFactory2,&p_DXGID3D10CreateDevice,&p_DXGID3D10CreateLayeredDevice,
    &p_DXGID3D10GetLayeredDeviceSize,&p_DXGID3D10RegisterLayers,&p_DXGIDeclareAdapterRemovalSupport,
    &p_DXGIDumpJournal,&p_DXGIGetDebugInterface1,&p_DXGIReportAdapterConfiguration,&p_PIXBeginCapture,
    &p_PIXEndCapture,&p_PIXGetCaptureState,&p_SetAppCompatStringPointer,&p_UpdateHMDEmulationStatus
};

static void ResolveRealDxgi()
{
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);                 // ...\Windows\System32
    std::wstring path = std::wstring(sys) + L"\\dxgi.dll";
    HMODULE real = LoadLibraryW(path.c_str());          // the genuine dxgi, by full path
    if (!real) return;
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i)
        *kPtrs[i] = reinterpret_cast<void*>(GetProcAddress(real, kNames[i]));
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinst);
        ResolveRealDxgi();   // forwards must work before the game touches dxgi
        GlyphSwap_Start();   // install the texture-swap hooks (worker thread)
    }
    return TRUE;
}
