// dllmain.cpp — PS4 button-prompt texture-swap mod for
// One Piece: World Seeker (Unreal Engine 4 / DirectX 11).
//
// Build as a 64-bit DLL. Requires MinHook (https://github.com/TsudaKageyu/minhook).
//
// HOW IT WORKS
// ------------
// DirectX 11 interfaces are COM objects: a pointer to a vtable (an array of
// function pointers) shared by every instance of that class in the process.
// We create a throwaway device/swapchain purely to read the vtable addresses,
// then patch four of them with MinHook. Because the vtable is shared, the
// game's real device is patched too.
//
//   CreateTexture2D            -> identify textures by CRC32 of their pixels
//   CreateShaderResourceView   -> map a tagged texture to its bindable SRV
//   PSSetShaderResources       -> swap our SRV in when the game binds the target
//   Present                    -> heartbeat / log "pipeline is live"
//
// Identification happens once per texture (cheap); swapping on the hot path is
// just an unordered_map lookup.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // allow _wfopen on MSVC without C4996
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <cstdarg>
#include <cstdint>
#include <cctype>
#include <cstdio>

#include "MinHook.h"
#include "Crc32.h"
#include "TextureLoader.h"

// d3d11/dxgi/windowscodecs are linked via the build script (works for both
// MSVC and MinGW). MSVC users can keep these pragmas instead:
#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Original-function typedefs and trampoline pointers (filled by MinHook).
// ---------------------------------------------------------------------------
typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* CreateTexture2D_t)(
    ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
typedef HRESULT(STDMETHODCALLTYPE* CreateSRV_t)(
    ID3D11Device*, ID3D11Resource*, const D3D11_SHADER_RESOURCE_VIEW_DESC*, ID3D11ShaderResourceView**);
typedef void(STDMETHODCALLTYPE* PSSetSRV_t)(
    ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);

static Present_t         oPresent          = nullptr;
static CreateTexture2D_t oCreateTexture2D  = nullptr;
static CreateSRV_t       oCreateSRV        = nullptr;
static PSSetSRV_t        oPSSetSRV         = nullptr;

// ---------------------------------------------------------------------------
// Paths, logging (Unicode-safe via wide stdio so it works under folders like
// "C:\Users\<non-ascii>\...").
// ---------------------------------------------------------------------------
static std::wstring g_modDir;   // <game folder>\PS4Mod
static std::wstring g_logPath;

static std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring Widen(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::mutex g_logMtx;
static void Log(const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lk(g_logMtx);
    FILE* f = _wfopen(g_logPath.c_str(), L"a");   // wide path: handles Unicode dirs
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

static void InitPaths()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);   // full path of the host .exe
    std::wstring p(exe);
    size_t slash = p.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    g_modDir = dir + L"\\PS4Mod";
    g_logPath = g_modDir + L"\\ps4mod_log.txt";
}

// ---------------------------------------------------------------------------
// Config (PS4Mod\config.ini).
// ---------------------------------------------------------------------------
struct Config
{
    bool dump = false;                                              // log every texture?
    std::unordered_map<uint32_t, std::wstring> hashToFile;          // 0xHASH -> file (exact)
    std::map<std::pair<uint32_t, uint32_t>, std::wstring> dimToFile;// WxH    -> file (discovery)
} g_cfg;

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string ToLower(std::string s)
{
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static void ParseConfig()
{
    std::wstring cfgPath = g_modDir + L"\\config.ini";

    // Read the whole file through wide stdio (portable + Unicode-safe), then
    // parse from memory. Avoids MSVC-only ifstream(wchar_t*) overloads.
    FILE* f = _wfopen(cfgPath.c_str(), L"rb");
    if (!f)
    {
        Log("No config.ini at %s (dump OFF, no replacements).", Narrow(cfgPath).c_str());
        return;
    }
    std::string content;
    char rb[4096];
    size_t got;
    while ((got = fread(rb, 1, sizeof(rb), f)) > 0) content.append(rb, got);
    fclose(f);

    std::istringstream in(content);
    std::string line, section;
    while (std::getline(in, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[') { section = ToLower(line); continue; }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        size_t cmt = val.find_first_of(";#");          // strip trailing inline comment
        if (cmt != std::string::npos) val = Trim(val.substr(0, cmt));
        std::string lkey = ToLower(key);

        if (section.find("settings") != std::string::npos)
        {
            if (lkey == "dumptextures") g_cfg.dump = (val == "1" || val == "true");
        }
        else if (section.find("replacements") != std::string::npos)
        {
            if (lkey.rfind("0x", 0) == 0)              // exact hash rule
            {
                uint32_t h = (uint32_t)strtoul(key.c_str(), nullptr, 16);
                g_cfg.hashToFile[h] = Widen(val);
            }
            else                                       // "WxH" dimension rule (discovery)
            {
                size_t xp = lkey.find('x');
                if (xp != std::string::npos)
                {
                    uint32_t w = (uint32_t)strtoul(key.substr(0, xp).c_str(), nullptr, 10);
                    uint32_t h = (uint32_t)strtoul(key.substr(xp + 1).c_str(), nullptr, 10);
                    g_cfg.dimToFile[{w, h}] = Widen(val);
                }
            }
        }
    }
    Log("Config loaded: dump=%d, hash-rules=%zu, dim-rules=%zu",
        g_cfg.dump ? 1 : 0, g_cfg.hashToFile.size(), g_cfg.dimToFile.size());
}

// ---------------------------------------------------------------------------
// Replacement bookkeeping.
//   g_resReplacement : game texture -> our replacement SRV
//   g_srvReplacement : game SRV     -> our replacement SRV
//   g_loaded         : file path    -> loaded SRV (cache; null entry == failed)
// A shared_mutex lets the render thread read concurrently while writes are rare.
// g_bypass (thread-local) stops our OWN texture loads from re-entering the hooks.
// ---------------------------------------------------------------------------
static std::shared_mutex g_mapMtx;
static std::unordered_map<ID3D11Resource*, ID3D11ShaderResourceView*> g_resReplacement;
static std::unordered_map<ID3D11ShaderResourceView*, ID3D11ShaderResourceView*> g_srvReplacement;
static std::unordered_map<std::wstring, ComPtr<ID3D11ShaderResourceView>> g_loaded;
static std::atomic<bool> g_hasReplacements{ false };
static thread_local bool g_bypass = false;

// BC1..BC7 are block-compressed: rows are counted in 4-pixel blocks.
static bool IsBlockCompressed(DXGI_FORMAT f)
{
    return (f >= DXGI_FORMAT_BC1_TYPELESS  && f <= DXGI_FORMAT_BC5_SNORM) ||
           (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

static ID3D11ShaderResourceView* GetOrLoadSRV(ID3D11Device* dev, const std::wstring& file)
{
    {   // fast path: already loaded (or already known-failed)
        std::shared_lock<std::shared_mutex> rd(g_mapMtx);
        auto it = g_loaded.find(file);
        if (it != g_loaded.end()) return it->second.Get();
    }

    std::unique_lock<std::shared_mutex> wr(g_mapMtx);
    auto it = g_loaded.find(file);                 // re-check under write lock
    if (it != g_loaded.end()) return it->second.Get();

    std::wstring full = g_modDir + L"\\" + file;
    ComPtr<ID3D11ShaderResourceView> srv;

    g_bypass = true;                               // don't hook our own creation calls
    HRESULT hr = LoadImageToSRV(dev, full.c_str(), srv);
    g_bypass = false;

    if (SUCCEEDED(hr)) Log("Loaded replacement '%s'", Narrow(full).c_str());
    else               Log("FAILED to load '%s' (hr=0x%08lX)", Narrow(full).c_str(), (unsigned long)hr);

    g_loaded[file] = srv;                          // cache success OR failure (null)
    return srv.Get();
}

// Decide whether a freshly created texture should be replaced, and with what.
static ID3D11ShaderResourceView* ResolveReplacement(ID3D11Device* dev,
                                                    uint32_t hash, UINT w, UINT h)
{
    auto hit = g_cfg.hashToFile.find(hash);
    if (hit != g_cfg.hashToFile.end()) return GetOrLoadSRV(dev, hit->second);

    auto dit = g_cfg.dimToFile.find({ w, h });
    if (dit != g_cfg.dimToFile.end()) return GetOrLoadSRV(dev, dit->second);

    return nullptr;
}

// ---------------------------------------------------------------------------
// Hook: CreateTexture2D — identify textures at upload time.
// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE hkCreateTexture2D(
    ID3D11Device* dev, const D3D11_TEXTURE2D_DESC* desc,
    const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** ppTex)
{
    HRESULT hr = oCreateTexture2D(dev, desc, init, ppTex);

    // Only inspect textures the game uploaded pixel data for (UI atlases are
    // typically immutable/initialized). Skip our own loads and any failures.
    if (g_bypass || FAILED(hr) || !desc || !init || !init->pSysMem || !ppTex || !*ppTex)
        return hr;

    UINT rows = IsBlockCompressed(desc->Format) ? (desc->Height + 3) / 4 : desc->Height;
    size_t bytes = (size_t)init->SysMemPitch * rows;       // mip 0 only
    uint32_t hash = bytes ? Crc32(init->pSysMem, bytes) : 0;

    if (g_cfg.dump)
        Log("[TEX] %4ux%-4u fmt=%-3d mips=%u hash=0x%08X",
            desc->Width, desc->Height, (int)desc->Format, desc->MipLevels, hash);

    if (ID3D11ShaderResourceView* repl = ResolveReplacement(dev, hash, desc->Width, desc->Height))
    {
        std::unique_lock<std::shared_mutex> wr(g_mapMtx);
        // ID3D11Texture2D and ID3D11Resource share one COM identity (single
        // inheritance), so this pointer equals the one passed to CreateSRV.
        g_resReplacement[static_cast<ID3D11Resource*>(*ppTex)] = repl;
        Log("[MATCH] %ux%u hash=0x%08X -> replacement bound",
            desc->Width, desc->Height, hash);
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Hook: CreateShaderResourceView — link a tagged texture to its SRV.
// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE hkCreateSRV(
    ID3D11Device* dev, ID3D11Resource* res,
    const D3D11_SHADER_RESOURCE_VIEW_DESC* d, ID3D11ShaderResourceView** ppSRV)
{
    HRESULT hr = oCreateSRV(dev, res, d, ppSRV);
    if (g_bypass || FAILED(hr) || !res || !ppSRV || !*ppSRV) return hr;

    ID3D11ShaderResourceView* repl = nullptr;
    {
        std::shared_lock<std::shared_mutex> rd(g_mapMtx);
        auto it = g_resReplacement.find(res);
        if (it != g_resReplacement.end()) repl = it->second;
    }
    if (repl)
    {
        std::unique_lock<std::shared_mutex> wr(g_mapMtx);
        g_srvReplacement[*ppSRV] = repl;
        g_hasReplacements.store(true, std::memory_order_relaxed);
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Hook: PSSetShaderResources — the swap. Substitute our SRV when the game binds
// a tagged one to the pixel shader. (DrawIndexed would also work, but binding
// is the natural, lower-overhead interception point.)
// ---------------------------------------------------------------------------
static void STDMETHODCALLTYPE hkPSSetSRV(
    ID3D11DeviceContext* ctx, UINT start, UINT num,
    ID3D11ShaderResourceView* const* ppSRV)
{
    // D3D11 allows at most 128 SRV slots in a single call.
    if (g_hasReplacements.load(std::memory_order_relaxed) && ppSRV && num > 0 && num <= 128)
    {
        ID3D11ShaderResourceView* local[128];
        bool swapped = false;
        {
            std::shared_lock<std::shared_mutex> rd(g_mapMtx);
            for (UINT i = 0; i < num; ++i)
            {
                ID3D11ShaderResourceView* s = ppSRV[i];
                if (s)
                {
                    auto it = g_srvReplacement.find(s);
                    if (it != g_srvReplacement.end()) { local[i] = it->second; swapped = true; continue; }
                }
                local[i] = s;
            }
        }
        if (swapped) { oPSSetSRV(ctx, start, num, local); return; }   // pass our copy
    }
    oPSSetSRV(ctx, start, num, ppSRV);                               // untouched
}

// ---------------------------------------------------------------------------
// Hook: Present — heartbeat so the log confirms the pipeline is live.
// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    static bool once = false;
    if (!once) { once = true; Log("First Present(): D3D11 pipeline active, mod ready."); }
    return oPresent(sc, sync, flags);
}

// ---------------------------------------------------------------------------
// Vtable acquisition + hook installation.
// Spin up a throwaway device/swapchain ONLY to read the shared vtable addresses,
// then hook those addresses. The dummy objects can be freed immediately
// afterward — the code they point to lives in d3d11.dll for the process lifetime.
// ---------------------------------------------------------------------------
static bool InstallHooks()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PS4ModDummyWnd";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 64;
    scd.BufferDesc.Height = 64;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    ComPtr<IDXGISwapChain> sc;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;

    // The dummy device exists ONLY to read vtable addresses, which are shared by
    // all D3D11 devices in the process regardless of driver type. Try hardware
    // first; fall back to WARP (software) so init still succeeds on machines
    // (or sandboxes) without a usable GPU.
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, ARRAYSIZE(fl),
        D3D11_SDK_VERSION, &scd, &sc, &dev, nullptr, &ctx);
    if (FAILED(hr))
    {
        Log("Hardware dummy device failed (hr=0x%08lX); trying WARP.", (unsigned long)hr);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fl, ARRAYSIZE(fl),
            D3D11_SDK_VERSION, &scd, &sc, &dev, nullptr, &ctx);
    }

    if (FAILED(hr))
    {
        Log("Dummy D3D11 device creation failed (hr=0x%08lX).", (unsigned long)hr);
        if (hwnd) DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // vtable = first pointer-sized field of the COM object.
    void** swapVtbl = *reinterpret_cast<void***>(sc.Get());
    void** devVtbl  = *reinterpret_cast<void***>(dev.Get());
    void** ctxVtbl  = *reinterpret_cast<void***>(ctx.Get());

    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed."); return false; }

    bool ok = true;
    ok &= MH_CreateHook(swapVtbl[8], reinterpret_cast<void*>(&hkPresent),        reinterpret_cast<void**>(&oPresent))         == MH_OK; // Present
    ok &= MH_CreateHook(devVtbl[5],  reinterpret_cast<void*>(&hkCreateTexture2D),reinterpret_cast<void**>(&oCreateTexture2D)) == MH_OK; // CreateTexture2D
    ok &= MH_CreateHook(devVtbl[7],  reinterpret_cast<void*>(&hkCreateSRV),      reinterpret_cast<void**>(&oCreateSRV))       == MH_OK; // CreateShaderResourceView
    ok &= MH_CreateHook(ctxVtbl[8],  reinterpret_cast<void*>(&hkPSSetSRV),       reinterpret_cast<void**>(&oPSSetSRV))        == MH_OK; // PSSetShaderResources
    ok &= MH_EnableHook(MH_ALL_HOOKS) == MH_OK;

    Log("Hook installation: %s", ok ? "OK" : "FAILED");

    if (hwnd) DestroyWindow(hwnd);            // dummies no longer needed
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

static DWORD WINAPI Bootstrap(LPVOID)
{
    InitPaths();
    Log("=== PS4 Button Mod starting ===");
    ParseConfig();                           // must run before hooks fire
    InstallHooks();
    return 0;
}

// extern "C" + WINAPI keeps the symbol name/linkage the CRT startup expects on
// both MSVC and MinGW-w64.
extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinst);
        // Do real work off the loader lock.
        CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
    }
    return TRUE;
}
