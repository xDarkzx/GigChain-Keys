# Builds the app's Windows icon (branding\app.ico, 16-256 px) from the icon
# picture: the chain-and-keys mark on its dark background, in a rounded
# tile. Run it again after changing the picture: tools\make-icon.ps1
param(
    [string]$Source = "$PSScriptRoot\..\branding\gigchain-icon.png",
    [string]$Out = "$PSScriptRoot\..\branding\app.ico",
    [string]$Preview = "$PSScriptRoot\..\branding\app-icon.png",
    # The part of the picture used (pixels: left, top, width, height): the
    # mark with its background round it.
    [int[]]$Mark = @(0, 0, 765, 542),
    # How wide the mark itself is in that part (pixels): it fills 84% of the tile.
    [int]$MarkWidth = 670,
    # How far in from the part's sides, and from its top and bottom, its
    # background fades into the tile (the mark itself must stay clear).
    [int]$Feather = 12,
    [int]$FeatherTopBottom = 80
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$logo = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Source))
try {
    $markRect = New-Object System.Drawing.Rectangle $Mark[0], $Mark[1], $Mark[2], $Mark[3]
    if ($markRect.Right -gt $logo.Width -or $markRect.Bottom -gt $logo.Height) {
        throw "The mark ($Mark) is outside the logo ($($logo.Width) x $($logo.Height))"
    }
    $cut = $logo.Clone($markRect, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    # The tile: the logo's background as it is round the mark (the average
    # of the cut-out's edge), a little lighter at the top, darker below, so
    # the cut-out meets it without a seam.
    $sum = @(0, 0, 0); $count = 0
    for ($x = 0; $x -lt $cut.Width; $x += 2) {
        foreach ($y in 0, ($cut.Height - 1)) { $c = $cut.GetPixel($x, $y); $sum[0] += $c.R; $sum[1] += $c.G; $sum[2] += $c.B; ++$count }
    }
    for ($y = 0; $y -lt $cut.Height; $y += 2) {
        foreach ($x in 0, ($cut.Width - 1)) { $c = $cut.GetPixel($x, $y); $sum[0] += $c.R; $sum[1] += $c.G; $sum[2] += $c.B; ++$count }
    }
    $edge = [System.Drawing.Color]::FromArgb(255, [int]($sum[0] / $count), [int]($sum[1] / $count), [int]($sum[2] / $count))
    $shade = { param($c, $f) [System.Drawing.Color]::FromArgb(255, [Math]::Min(255, [int]($c.R * $f)), [Math]::Min(255, [int]($c.G * $f)), [Math]::Min(255, [int]($c.B * $f))) }
    $top = & $shade $edge 1.15
    $bottom = & $shade $edge 0.7

    # The mark cut out with soft edges: the glow round it fades into the
    # tile instead of ending in a hard box.
    $smooth = { param($v) $v = [Math]::Min(1.0, $v); $v * $v * (3 - 2 * $v) }
    for ($y = 0; $y -lt $cut.Height; ++$y) {
        $ty = & $smooth ([Math]::Min($y, $cut.Height - 1 - $y) / [double]$FeatherTopBottom)
        for ($x = 0; $x -lt $cut.Width; ++$x) {
            $tx = & $smooth ([Math]::Min($x, $cut.Width - 1 - $x) / [double]$Feather)
            $t = $tx * $ty
            if ($t -ge 1.0) { continue }
            $c = $cut.GetPixel($x, $y)
            $cut.SetPixel($x, $y, [System.Drawing.Color]::FromArgb([int]($c.A * $t), $c.R, $c.G, $c.B))
        }
    }

    $images = @()
    foreach ($size in 256, 128, 64, 48, 32, 24, 16) {
        $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)

        # The rounded dark square.
        $r = [Math]::Max(2, $size * 0.18)
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $d = 2 * $r
        $path.AddArc(0, 0, $d, $d, 180, 90)
        $path.AddArc($size - $d - 1, 0, $d, $d, 270, 90)
        $path.AddArc($size - $d - 1, $size - $d - 1, $d, $d, 0, 90)
        $path.AddArc(0, $size - $d - 1, $d, $d, 90, 90)
        $path.CloseFigure()
        $fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Rectangle 0, 0, $size, $size), $top, $bottom, 90
        $g.FillPath($fill, $path)

        # The mark across 84% of the tile, in the middle; its soft surround
        # may run off the edge.
        $width = $size * 0.84 * $markRect.Width / $MarkWidth
        $height = $width * $markRect.Height / $markRect.Width
        $dest = New-Object System.Drawing.RectangleF (($size - $width) / 2), (($size - $height) / 2), $width, $height
        $g.SetClip($path)
        $g.DrawImage($cut, $dest)
        $g.Dispose()

        # 256 px as PNG; the smaller sizes as bitmaps (a 32-bit DIB), which
        # every part of Windows reads.
        $stream = New-Object System.IO.MemoryStream
        if ($size -eq 256) {
            $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png)
        } else {
            $lock = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $size, $size),
                [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $pixels = New-Object byte[] ($lock.Stride * $size)
            [System.Runtime.InteropServices.Marshal]::Copy($lock.Scan0, $pixels, 0, $pixels.Length)
            $bmp.UnlockBits($lock)
            $dib = New-Object System.IO.BinaryWriter $stream
            # BITMAPINFOHEADER: the height counts the colour rows and the mask rows.
            $dib.Write([uint32]40); $dib.Write([int32]$size); $dib.Write([int32]($size * 2))
            $dib.Write([uint16]1); $dib.Write([uint16]32); $dib.Write([uint32]0)
            $dib.Write([uint32]($size * $size * 4)); $dib.Write([int32]0); $dib.Write([int32]0)
            $dib.Write([uint32]0); $dib.Write([uint32]0)
            for ($row = $size - 1; $row -ge 0; --$row) { # bottom row first, BGRA
                $dib.Write($pixels, $row * $lock.Stride, $size * 4)
            }
            $maskRow = [int]([Math]::Ceiling($size / 32.0) * 4) # the AND mask: unused (alpha is), all clear
            $dib.Write((New-Object byte[] ($maskRow * $size)))
            $dib.Flush()
        }
        $bmp.Dispose()
        $images += , @($size, $stream.ToArray())
    }

    # An .ico holding one PNG per size: header, directory, then the pictures.
    $file = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $file
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)
    $offset = 6 + (16 * $images.Count)
    foreach ($image in $images) {
        $size = $image[0]; $bytes = $image[1]
        $w.Write([byte]($size % 256)); $w.Write([byte]($size % 256)) # 256 is written as 0
        $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([uint16]1); $w.Write([uint16]32)
        $w.Write([uint32]$bytes.Length); $w.Write([uint32]$offset)
        $offset += $bytes.Length
    }
    foreach ($image in $images) { $w.Write([byte[]]$image[1]) }
    $w.Flush()
    [System.IO.File]::WriteAllBytes((Join-Path (Resolve-Path (Split-Path $Out)) (Split-Path $Out -Leaf)), $file.ToArray())
    "Wrote $Out ($($images.Count) sizes, $($file.Length) bytes) and $Preview"
} finally {
    if ($cut) { $cut.Dispose() }
    $logo.Dispose()
}
