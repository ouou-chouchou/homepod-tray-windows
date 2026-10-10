# Render the vector design with Windows GDI+; no downloaded assets or fonts.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
function Round-Rect([float]$x,[float]$y,[float]$w,[float]$h,[float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($x,$y,$d,$d,180,90)
    $path.AddArc(($x+$w-$d),$y,$d,$d,270,90)
    $path.AddArc(($x+$w-$d),($y+$h-$d),$d,$d,0,90)
    $path.AddArc($x,($y+$h-$d),$d,$d,90,90)
    $path.CloseFigure()
    return $path
}
function Render-Icon([int]$size,[bool]$warning) {
    $renderSize = [Math]::Max(512,$size*4)
    $bitmap = New-Object System.Drawing.Bitmap($renderSize,$renderSize)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform(($renderSize/256.0),($renderSize/256.0))
    $background = Round-Rect 8 8 240 240 57
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush([System.Drawing.Point]::new(8,8),[System.Drawing.Point]::new(248,248),[System.Drawing.ColorTranslator]::FromHtml('#215e96'),[System.Drawing.ColorTranslator]::FromHtml('#11223e'))
    $g.FillPath($gradient,$background)
    $border = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(46,139,207,255),2)
    $g.DrawPath($border,$background)
    $wave = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml('#71e1ed'),9)
    $wave.StartCap = $wave.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawBezier($wave,77,67,111,40,145,40,179,67)
    $g.DrawBezier($wave,96,84,117,67,139,67,160,84)
    $shadow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(77,6,19,41))
    $g.FillEllipse($shadow,83,209,90,14)
    $speaker = Round-Rect 86 109 84 100 30
    $body = New-Object System.Drawing.Drawing2D.LinearGradientBrush([System.Drawing.Point]::new(86,109),[System.Drawing.Point]::new(170,209),[System.Drawing.ColorTranslator]::FromHtml('#f6fbff'),[System.Drawing.ColorTranslator]::FromHtml('#b7d4e6'))
    $g.FillPath($body,$speaker)
    $top = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#daedf5'))
    $g.FillEllipse($top,93,104,70,24)
    $screen = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#417a98'))
    $g.FillEllipse($screen,104,109,48,12)
    $light = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#84edf0'))
    $g.FillEllipse($light,116,111,24,6)
    $mesh = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(69,49,95,124))
    if ($size -ge 32) { foreach($y in 146,159,172,185) { foreach($x in 108,118,128,138,148) { $g.FillEllipse($mesh,($x-2),($y-2),4,4) } } }
    if ($warning) {
        $rim = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#11223e'))
        $amber = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#ffc45c'))
        $g.FillEllipse($rim,172,172,68,68)
        $g.FillEllipse($amber,179,179,54,54)
        $mark = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml('#11223e'),6)
        $mark.StartCap = $mark.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $g.DrawLine($mark,206,191,206,208)
        $g.FillEllipse($rim,203,215,6,6)
        $rim.Dispose(); $amber.Dispose(); $mark.Dispose()
    }
    foreach($item in @($background,$gradient,$border,$wave,$shadow,$speaker,$body,$top,$screen,$light,$mesh)) { $item.Dispose() }
    $g.Dispose()
    $small = New-Object System.Drawing.Bitmap($size,$size)
    $target = [System.Drawing.Graphics]::FromImage($small)
    $target.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $target.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $target.DrawImage($bitmap,0,0,$size,$size)
    $target.Dispose(); $bitmap.Dispose()
    return $small
}
function Save-Ico([string]$file,[bool]$warning) {
    $sizes = @(16,20,24,32,40,48,64,128,256)
    $images = @()
    foreach($size in $sizes) {
        $bmp = Render-Icon $size $warning
        $memory = New-Object System.IO.MemoryStream
        $bmp.Save($memory,[System.Drawing.Imaging.ImageFormat]::Png)
        $images += ,$memory.ToArray()
        $memory.Dispose(); $bmp.Dispose()
    }
    $stream = [System.IO.File]::Create($file)
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16*$sizes.Count
        for($i=0;$i -lt $sizes.Count;$i++) {
            $dimension = if($sizes[$i] -eq 256) {0} else {$sizes[$i]}
            $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
            $offset += $images[$i].Length
        }
        foreach($bytes in $images) { $writer.Write([byte[]]$bytes) }
    } finally { $writer.Dispose(); $stream.Dispose() }
}
Save-Ico (Join-Path $PSScriptRoot 'homepod.ico') $false
Save-Ico (Join-Path $PSScriptRoot 'homepod-warning.ico') $true
$preview = Render-Icon 512 $false
$preview.Save((Join-Path $PSScriptRoot 'homepod-preview.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()
