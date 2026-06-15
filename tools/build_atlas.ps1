# build_atlas.ps1 — compose individual button-glyph PNGs (e.g. from a CC0
# controller-prompt pack) into ONE atlas PNG that the mod swaps in.
#
# A pack gives you one file per button. The game samples a single atlas texture
# with fixed UV cells, so we stitch the pack's icons onto a canvas of the exact
# size the original atlas uses, each placed in the cell it must occupy.
#
# Usage:
#   .\tools\build_atlas.ps1                         # uses defaults below
#   .\tools\build_atlas.ps1 -Layout my.ini -GlyphDir art -Out GlyphSwap\ps4_buttons.png
#
# Workflow:
#   1) Drop the pack's PNGs into  tools\glyphs\
#   2) Run the discovery step (DumpTextures=1) to learn the original atlas's
#      WIDTHxHEIGHT and each glyph's cell (x, y, w, h).
#   3) Edit tools\atlas_layout.ini to match, then run this script.
#   4) Point config.ini at the produced GlyphSwap\ps4_buttons.png.
param(
    [string]$Layout  = "$PSScriptRoot\atlas_layout.ini",
    [string]$GlyphDir= "$PSScriptRoot\glyphs",
    [string]$Out      = "$PSScriptRoot\..\GlyphSwap\ps4_buttons.png"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Layout)) { throw "Layout not found: $Layout" }

# ---- parse the INI ---------------------------------------------------------
$atlasW = 1024; $atlasH = 1024; $bg = $null
$glyphs = New-Object System.Collections.Generic.List[object]
$section = ""
foreach ($raw in Get-Content -LiteralPath $Layout) {
    $line = $raw.Trim()
    if ($line -eq "" -or $line.StartsWith(";") -or $line.StartsWith("#")) { continue }
    if ($line.StartsWith("[")) { $section = $line.Trim('[',']').ToLower(); continue }
    $eq = $line.IndexOf('='); if ($eq -lt 0) { continue }
    $key = $line.Substring(0,$eq).Trim()
    $val = $line.Substring($eq+1).Trim()
    if ($section -eq "atlas") {
        switch ($key.ToLower()) {
            "width"      { $atlasW = [int]$val }
            "height"     { $atlasH = [int]$val }
            "background" { $bg = $val }   # e.g. 00000000 (RRGGBBAA) or blank = transparent
        }
    }
    elseif ($section -eq "glyphs") {
        # name = file, x, y, w, h
        $p = $val.Split(',') | ForEach-Object { $_.Trim() }
        if ($p.Count -lt 5) { Write-Warning "Skipping '$key' (need file,x,y,w,h)"; continue }
        $glyphs.Add([pscustomobject]@{ name=$key; file=$p[0]; x=[int]$p[1]; y=[int]$p[2]; w=[int]$p[3]; h=[int]$p[4] })
    }
}

Write-Host ("Atlas: {0}x{1}, {2} glyph(s)" -f $atlasW,$atlasH,$glyphs.Count) -ForegroundColor Cyan

# ---- compose ---------------------------------------------------------------
$bmp = New-Object System.Drawing.Bitmap($atlasW,$atlasH,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g   = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = 'HighQualityBicubic'   # clean scaling of pack icons
$g.PixelOffsetMode   = 'HighQuality'
$g.SmoothingMode     = 'AntiAlias'

if ($bg) {
    $r=[Convert]::ToInt32($bg.Substring(0,2),16); $gn=[Convert]::ToInt32($bg.Substring(2,2),16)
    $b=[Convert]::ToInt32($bg.Substring(4,2),16); $a=[Convert]::ToInt32($bg.Substring(6,2),16)
    $g.Clear([System.Drawing.Color]::FromArgb($a,$r,$gn,$b))
} else {
    $g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))   # transparent
}

$placed = 0; $missing = @()
foreach ($gl in $glyphs) {
    $path = Join-Path $GlyphDir $gl.file
    if (-not (Test-Path $path)) { $missing += $gl.file; continue }
    $img = [System.Drawing.Image]::FromFile($path)
    try {
        $dest = New-Object System.Drawing.Rectangle($gl.x,$gl.y,$gl.w,$gl.h)
        $g.DrawImage($img,$dest,0,0,$img.Width,$img.Height,[System.Drawing.GraphicsUnit]::Pixel)
        $placed++
    } finally { $img.Dispose() }
}
$g.Dispose()

$outFull = [System.IO.Path]::GetFullPath($Out)
$bmp.Save($outFull,[System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

Write-Host ("Placed {0}/{1} glyphs -> {2}" -f $placed,$glyphs.Count,$outFull) -ForegroundColor Green
if ($missing.Count) { Write-Warning ("Missing files in {0}: {1}" -f $GlyphDir, ($missing -join ', ')) }
