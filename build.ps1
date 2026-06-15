# build.ps1 — build GlyphSwap.dll + Injector.exe with MinGW-w64 (GCC),
# without needing CMake. Run from the project root:  .\build.ps1
#
# Output goes to .\dist\ :  GlyphSwap.dll, Injector.exe
#
# NOTE on non-ASCII paths: the GNU toolchain mishandles Unicode in both the
# build directory AND its own install directory (it renders wide paths through
# the ANSI codepage and feeds mojibake to the assembler/linker). This script
# works around both: it builds via the project's 8.3 short path, and if the
# compiler itself lives under a non-ASCII path it mirrors the toolchain to an
# ASCII location (C:\mgw) once.
$ErrorActionPreference = "Stop"

# Resolve the project root to its 8.3 short path (pure ASCII) so the assembler
# can write object files even under a Unicode user-profile folder.
$fso  = New-Object -ComObject Scripting.FileSystemObject
$root = $fso.GetFolder($PSScriptRoot).ShortPath

# --- locate an ASCII-path GCC toolchain ------------------------------------
function Resolve-AsciiToolchain {
    if (Test-Path "C:\mgw\bin\g++.exe") { return "C:\mgw" }     # existing mirror

    $src = $null
    $c = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($c) { $src = Split-Path (Split-Path $c.Source) }        # ...\bin\g++ -> ...\mingw64
    if (-not $src) {
        $guess = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64"
        if (Test-Path $guess) { $src = $guess }
    }
    if (-not $src) { throw "MinGW-w64 (g++) not found. Install it (winget install BrechtSanders.WinLibs.POSIX.UCRT) or add it to PATH." }

    if (-not ($src -match '[^\x20-\x7e]')) { return $src }       # already ASCII

    Write-Host "Compiler path is non-ASCII; mirroring toolchain to C:\mgw (~900 MB, one-time)..." -ForegroundColor Yellow
    robocopy $src "C:\mgw" /E /MT:16 /NFL /NDL /NJH /NJS /NP | Out-Null
    if (-not (Test-Path "C:\mgw\bin\g++.exe")) { throw "Failed to mirror toolchain to C:\mgw." }
    return "C:\mgw"
}
$tc  = Resolve-AsciiToolchain
$gpp = Join-Path $tc "bin\g++.exe"
$gcc = Join-Path $tc "bin\gcc.exe"
Write-Host "Using compiler: $gpp" -ForegroundColor Cyan

$mh   = Join-Path $root "third_party\minhook"
$dist = Join-Path $root "dist"
$obj  = Join-Path $root "build\obj"
New-Item -ItemType Directory -Force -Path $dist, $obj | Out-Null

# --- compile MinHook (C) ----------------------------------------------------
$mhSrc = @(
    "src\buffer.c", "src\hook.c", "src\trampoline.c",
    "src\hde\hde32.c", "src\hde\hde64.c"
)
$mhObjs = @()
foreach ($s in $mhSrc) {
    $o = Join-Path $obj ((Split-Path $s -Leaf) -replace '\.c$', '.o')
    Write-Host "  CC  $s"
    & $gcc -c -O2 -DNDEBUG "$mh\$s" -o $o
    if ($LASTEXITCODE -ne 0) { throw "MinHook compile failed: $s" }
    $mhObjs += $o
}

# --- compile + link the mod DLL --------------------------------------------
Write-Host "  CXX dllmain.cpp -> GlyphSwap.dll"
$dllArgs = @(
    "-shared", "-O2", "-std=c++17", "-DNDEBUG",
    "-DUNICODE", "-D_UNICODE", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
    "-I", "$root\src", "-I", "$mh\include",
    "$root\src\dllmain.cpp"
) + $mhObjs + @(
    "-o", "$dist\GlyphSwap.dll",
    "-static", "-static-libgcc", "-static-libstdc++",
    "-ld3d11", "-ldxgi", "-lwindowscodecs", "-lole32", "-loleaut32", "-luuid"
)
& $gpp @dllArgs
if ($LASTEXITCODE -ne 0) { throw "DLL link failed." }

# --- compile + link the injector -------------------------------------------
Write-Host "  CXX injector\main.cpp -> Injector.exe"
$injArgs = @(
    "-O2", "-std=c++17", "-municode",
    "-DUNICODE", "-D_UNICODE",
    "$root\injector\main.cpp",
    "-o", "$dist\Injector.exe",
    "-static", "-static-libgcc", "-static-libstdc++"
)
& $gpp @injArgs
if ($LASTEXITCODE -ne 0) { throw "Injector link failed." }

Write-Host "`nBuild OK ->" -ForegroundColor Green
Get-ChildItem $dist | Select-Object Length, Name
