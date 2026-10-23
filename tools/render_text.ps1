# render_text.ps1 -- rasterise RGSS-style dialogue text with the game's own font.
#
# Python cannot rasterise a TTF without third-party packages, so this uses
# System.Drawing (present on every Windows box) to produce a transparent RGBA
# text layer at the game's real geometry. make_ui_mockup.py composites it.
#
# Usage: powershell -ExecutionPolicy Bypass -File render_text.ps1 -Spec spec.json -Out layer.png
#   spec.json: { "w":640, "h":480, "font":"C:\\path\\OpenSans.ttf",
#                "items":[ {"text":"Hello","x":100,"y":320,"size":22} ] }

param(
    [Parameter(Mandatory = $true)][string]$SpecPath,
    [Parameter(Mandatory = $true)][string]$OutPath
)

Add-Type -AssemblyName System.Drawing

$spec = Get-Content -Path $SpecPath -Raw | ConvertFrom-Json

$w = [int]$spec.w
$h = [int]$spec.h
$bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.Clear([System.Drawing.Color]::Transparent)
$gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias

$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile($spec.font)
$family = $fonts.Families[0]

$shadowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(170, 0, 0, 0))
$textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)

foreach ($item in $spec.items) {
    $size = [double]$item.size
    $font = New-Object System.Drawing.Font($family, $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $x = [float]$item.x
    $y = [float]$item.y
    # RGSS draws text with a soft shadow; approximate it with a 1px offset copy
    $gfx.DrawString($item.text, $font, $shadowBrush, ($x + 1), ($y + 1))
    $gfx.DrawString($item.text, $font, $textBrush, $x, $y)
    $font.Dispose()
}

$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)

$shadowBrush.Dispose()
$textBrush.Dispose()
$gfx.Dispose()
$bmp.Dispose()

Write-Output ("rendered {0} text items to {1}" -f $spec.items.Count, $OutPath)
