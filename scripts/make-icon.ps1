# Draws the Driftwall logo and writes assets\Driftwall.ico, assets\logo.png and assets\icon-preview.png.
Add-Type -AssemblyName System.Drawing
$out = Join-Path (Split-Path $PSScriptRoot) 'assets'
New-Item -ItemType Directory -Force $out | Out-Null

function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

# A filled wave band: sine curve on top, flat bottom at y=248.
function New-Wave([float]$baseY, [float]$amp, [float]$phase, [float]$freq) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $pts = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    for ($x = 0; $x -le 256; $x += 4) {
        $pts.Add((New-Object System.Drawing.PointF $x, ($baseY - $amp * [Math]::Sin(($x / 256.0) * [Math]::PI * $freq + $phase))))
    }
    $pts.Add((New-Object System.Drawing.PointF 256, 256))
    $pts.Add((New-Object System.Drawing.PointF 0, 256))
    $p.AddPolygon($pts.ToArray())
    return $p
}

function New-Logo([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.ScaleTransform($size / 256.0, $size / 256.0)

    $tile = New-RoundRect 8 8 240 240 56
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF 8, 8), (New-Object System.Drawing.PointF 248, 248),
        [System.Drawing.Color]::FromArgb(255, 34, 211, 238), [System.Drawing.Color]::FromArgb(255, 79, 70, 229))
    $g.FillPath($grad, $tile)
    $g.SetClip($tile)

    # Soft moon
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 255, 255, 255))), 160, 46, 46, 46)

    # Three drifting hills, back to front
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(110, 255, 255, 255))), (New-Wave 150 22 0.6 2.0))
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 49, 46, 129))), (New-Wave 180 20 2.4 1.6))
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 23, 22, 66))), (New-Wave 212 16 4.2 1.3))
    $g.Dispose()
    return $bmp
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Logo $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    if ($s -eq 256) { $bmp.Save((Join-Path $out 'logo.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
    $pngs += , $ms.ToArray()
}

$fs = [System.IO.File]::Create((Join-Path $out 'Driftwall.ico'))
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $b = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$b); $w.Write([byte]$b); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$pngs[$i].Length); $w.Write([UInt32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
$w.Close()

# Preview: small sizes at 1:1 plus a large one, on dark and light backgrounds
$sheet = New-Object System.Drawing.Bitmap 520, 300
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(32, 32, 32))), 0, 0, 520, 150)
$g.FillRectangle([System.Drawing.Brushes]::WhiteSmoke, 0, 150, 520, 150)
foreach ($row in 0, 150) {
    $x = 12
    foreach ($s in 16, 24, 32, 48, 64) {
        $bmp = New-Logo $s
        $g.DrawImage($bmp, $x, $row + 75 - $s / 2, $s, $s)
        $x += $s + 16
        $bmp.Dispose()
    }
    $big = New-Logo 128
    $g.DrawImage($big, 370, $row + 11, 128, 128)
    $big.Dispose()
}
$g.Dispose()
$sheet.Save((Join-Path $out 'icon-preview.png'), [System.Drawing.Imaging.ImageFormat]::Png)
