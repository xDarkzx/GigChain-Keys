# Builds the installer's pictures from the branding: the tall picture down
# the side of the welcome and finish pages (the splash's logo and name on its
# own dark background) and the small one at the top right of the other pages
# (the app icon). Each at 100% and 200% for sharp high-DPI screens. Run it
# again after changing the branding: tools\make-installer-art.ps1
param(
    [string]$Splash = "$PSScriptRoot\..\branding\splash.png",
    [string]$Icon = "$PSScriptRoot\..\branding\app-icon.png",
    [string]$OutDir = "$PSScriptRoot\..\installer",
    # The splash's logo and name (pixels: left, top, width, height).
    [int[]]$Logo = @(150, 55, 440, 350),
    # How far in from the cut-out's edges its background fades out.
    [int]$Feather = 60
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Save-Bmp([System.Drawing.Bitmap]$image, [string]$path) {
    # 24-bit: what every Windows version shows the same way.
    $flat = New-Object System.Drawing.Bitmap $image.Width, $image.Height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($flat)
    $g.DrawImage($image, 0, 0, $image.Width, $image.Height)
    $g.Dispose()
    $flat.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $flat.Dispose()
    Write-Output "Wrote $path ($($image.Width) x $($image.Height))"
}

$splashImage = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Splash))
$iconImage = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Icon))
try {
    $rect = New-Object System.Drawing.Rectangle $Logo[0], $Logo[1], $Logo[2], $Logo[3]
    if ($rect.Right -gt $splashImage.Width -or $rect.Bottom -gt $splashImage.Height) {
        throw "The logo ($Logo) is outside the splash ($($splashImage.Width) x $($splashImage.Height))"
    }
    $cut = $splashImage.Clone($rect, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    # The background: the colour round the logo (the average of the
    # cut-out's edge), a little lighter above and darker below, so the
    # cut-out meets it without a seam.
    $sum = @(0, 0, 0); $count = 0
    for ($x = 0; $x -lt $cut.Width; $x += 2) {
        foreach ($y in 0, ($cut.Height - 1)) { $c = $cut.GetPixel($x, $y); $sum[0] += $c.R; $sum[1] += $c.G; $sum[2] += $c.B; ++$count }
    }
    for ($y = 0; $y -lt $cut.Height; $y += 2) {
        foreach ($x in 0, ($cut.Width - 1)) { $c = $cut.GetPixel($x, $y); $sum[0] += $c.R; $sum[1] += $c.G; $sum[2] += $c.B; ++$count }
    }
    $edge = [System.Drawing.Color]::FromArgb(255, [int]($sum[0] / $count), [int]($sum[1] / $count), [int]($sum[2] / $count))
    $shade = { param($c, $f) [System.Drawing.Color]::FromArgb(255, [Math]::Min(255, [int]($c.R * $f)), [Math]::Min(255, [int]($c.G * $f)), [Math]::Min(255, [int]($c.B * $f))) }
    $top = & $shade $edge 1.25
    $bottom = & $shade $edge 0.8

    # Soft edges: the cut-out's background fades into the panel's.
    $smooth = { param($v) $v = [Math]::Min(1.0, $v); $v * $v * (3 - 2 * $v) }
    for ($y = 0; $y -lt $cut.Height; ++$y) {
        $ty = & $smooth ([Math]::Min($y, $cut.Height - 1 - $y) / [double]$Feather)
        for ($x = 0; $x -lt $cut.Width; ++$x) {
            $tx = & $smooth ([Math]::Min($x, $cut.Width - 1 - $x) / [double]$Feather)
            $t = $tx * $ty
            if ($t -ge 1.0) { continue }
            $c = $cut.GetPixel($x, $y)
            $cut.SetPixel($x, $y, [System.Drawing.Color]::FromArgb([int]($c.A * $t), $c.R, $c.G, $c.B))
        }
    }

    foreach ($scale in 1, 2) {
        # The side picture: 164 x 314 at 100%.
        $w = 164 * $scale; $h = 314 * $scale
        $side = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($side)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $area = New-Object System.Drawing.Rectangle 0, 0, $w, $h
        $fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush $area, $top, $bottom, 90.0
        $g.FillRectangle($fill, $area)
        $fill.Dispose()
        $logoWidth = [int]($w * 0.94)
        $logoHeight = [int]($logoWidth * $cut.Height / $cut.Width)
        $g.DrawImage($cut, [int](($w - $logoWidth) / 2), [int](($h - $logoHeight) * 0.4), $logoWidth, $logoHeight)
        $g.Dispose()
        $suffix = if ($scale -eq 1) { '' } else { "@$($scale)x" }
        Save-Bmp $side (Join-Path $OutDir "wizard-side$suffix.bmp")
        $side.Dispose()

        # The small picture: the app icon, 55 x 55 at 100%, on the page's white.
        $s = 55 * $scale
        $small = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($small)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.Clear([System.Drawing.Color]::White)
        $g.DrawImage($iconImage, 0, 0, $s, $s)
        $g.Dispose()
        Save-Bmp $small (Join-Path $OutDir "wizard-small$suffix.bmp")
        $small.Dispose()
    }
    $cut.Dispose()
} finally {
    $splashImage.Dispose()
    $iconImage.Dispose()
}
