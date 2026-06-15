# PS4 Button Mod — One Piece: World Seeker

A from-scratch C++ DLL that hooks the game's **DirectX 11** rendering pipeline,
identifies the **Xbox button-prompt texture** in memory, and swaps it for your
own **PlayStation 4 / DualShock** artwork on the fly. No Special K, no PAK
extraction — just a small injected DLL and a console injector.

> Single-player UI mod for a game you own. It replaces on-screen button glyphs
> only; it does not touch save data, multiplayer, or anti-tamper.

---

## How it works (30-second version)

DirectX 11 objects are COM interfaces — each is a pointer to a **vtable**, an
array of function pointers **shared by every instance of that class** in the
process. So we spin up one throwaway D3D11 device purely to read four vtable
addresses, then patch them with [MinHook](https://github.com/TsudaKageyu/minhook).
Because the vtable is shared, the **game's real device is patched too**.

| Hooked function | vtbl # | Role |
|---|---|---|
| `IDXGISwapChain::Present` | 8 | Heartbeat / "pipeline live" log |
| `ID3D11Device::CreateTexture2D` | 5 | **Identify**: CRC32 each uploaded texture |
| `ID3D11Device::CreateShaderResourceView` | 7 | Map a tagged texture → its bindable SRV |
| `ID3D11DeviceContext::PSSetShaderResources` | 8 | **Swap**: bind our SRV when the game binds the target |

Identification happens once per texture (cheap CRC32 at upload). Swapping on the
render hot-path is just an `unordered_map` lookup. Your replacement PNG is
decoded to a GPU texture once via **WIC** (built into Windows — no image libs).

See [`src/dllmain.cpp`](src/dllmain.cpp) for the heavily-commented implementation.

---

## Repository layout

```
PS4DUALSHOCK_Layout/
├─ src/
│  ├─ dllmain.cpp        # config, logging, the 4 DX11 hooks, bootstrap, DllMain
│  ├─ Crc32.h            # table-based CRC32 (texture fingerprinting)
│  └─ TextureLoader.h    # WIC: PNG/JPG/BMP/TIFF -> ID3D11ShaderResourceView
├─ injector/
│  └─ main.cpp           # LoadLibrary + CreateRemoteThread console injector
├─ tools/
│  ├─ build_atlas.ps1    # stitch individual pack glyphs into one atlas PNG
│  ├─ atlas_layout.ini   # cell layout for the compositor (edit after discovery)
│  └─ glyphs/            # drop a CC0 prompt pack's PNGs here
├─ third_party/
│  └─ minhook/           # MinHook (git submodule / clone) — built from source
├─ PS4Mod/                    # ← ships next to the game .exe at runtime
│  ├─ config.ini              # dump toggle + replacement rules
│  ├─ debug_magenta.png       # solid-magenta tile for the discovery workflow
│  └─ ps4_buttons_template.png# starter glyph art (rework to match the atlas)
├─ CMakeLists.txt        # MSVC or MinGW build
├─ build.ps1             # one-command MinGW build (no CMake needed)
├─ INSTALL.md            # full setup, build, and usage guide
└─ README.md            # this file
```

After a build, artifacts land in **`dist/`**: `PS4ButtonMod.dll`, `Injector.exe`.

---

## Quick start

### 1. Build

**MinGW-w64 (one command):**
```powershell
.\build.ps1
```

**or CMake (MSVC or MinGW):**
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Full details, including Visual Studio, are in **[INSTALL.md](INSTALL.md)**.

### 2. Install into the game folder

Copy next to `OPWS.exe` (e.g. `...\Game\Binaries\Win64\`):

```
OPWS.exe
PS4ButtonMod.dll
Injector.exe
PS4Mod\
  config.ini
  ps4_buttons.png        # your artwork (added after discovery)
  debug_magenta.png
```

### 3. Find the texture, then swap it

1. In `PS4Mod\config.ini` keep `DumpTextures=1`. Launch the game, inject
   (`Injector.exe`), and walk to a screen showing button prompts.
2. Open `PS4Mod\ps4mod_log.txt`. Find the `[TEX]` line for the prompt atlas
   (square power-of-two; `BC7`=fmt 98, `BC3`=fmt 77, RGBA8=fmt 28). Confirm
   visually with a `1024x1024 = debug_magenta.png` rule if unsure.
3. Pin it by hash and turn dumping off:
   ```ini
   [Settings]
   DumpTextures=0

   [Replacements]
   0xA1B2C3D4 = ps4_buttons.png
   ```

The full discovery walkthrough is in **[INSTALL.md](INSTALL.md#5-finding-the-xbox-button-texture)**.

---

## Build & validation status

Built and verified with **MinGW-w64 GCC 15.2 (x64, UCRT)** on Windows 10 22H2:

- ✅ `PS4ButtonMod.dll` — 64-bit PE, imports `d3d11.dll`, MinHook linked statically.
- ✅ `Injector.exe` — 64-bit console injector.
- ✅ Load test: DLL injected into a host process → bootstrap ran → **all four
  DX11 hooks installed OK** (dummy device falls back to WARP when no GPU).
- ✅ WIC path: PNG decoded → `R8G8B8A8` texture + SRV created successfully.

The only piece that requires the actual game is matching OPWS's specific Xbox
atlas hash — that is the discovery step you perform once in step 3 above.

---

## Notes & limits

- **64-bit only.** `OPWS.exe` is an x64 UE4 process; an x86 build will not inject.
- **DDS replacements** aren't decoded by WIC — use PNG, or drop in Microsoft's
  `DDSTextureLoader` and call it from `GetOrLoadSRV`. See INSTALL.md.
- **Pointer-reuse caveat:** identification caches COM pointers; UI atlases are
  long-lived so this is rarely an issue in practice. Details in INSTALL.md.
- Authorized, single-player UI modding only.
