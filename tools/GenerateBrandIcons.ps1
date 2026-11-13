# Regenerate the platform icon sizes from the supplied transparent logo.
# Generated PNG/ICO files are checked in; builds do not require PowerShell.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$brandDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../assets/branding'))
$logo = [System.Drawing.Image]::FromFile((Join-Path $brandDirectory 'Minvex_logo_transparent.png'))
$sizes = @(16, 32, 64, 256)
try {
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($logo, [System.Drawing.Rectangle]::new(0, 0, $size, $size))
            $bitmap.Save((Join-Path $brandDirectory "Minvex_icon_$size.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    $logo.Dispose()
}

# Modern Windows ICO supports lossless PNG entries with their alpha channel.
$stream = [System.IO.File]::Create((Join-Path $brandDirectory 'Minvex.ico'))
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    foreach ($size in $sizes) {
        $png = [System.IO.File]::ReadAllBytes((Join-Path $brandDirectory "Minvex_icon_$size.png"))
        $dimension = if ($size -eq 256) { 0 } else { $size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$png.Length)
        $writer.Write([uint32]$offset)
        $offset += $png.Length
    }
    foreach ($size in $sizes) {
        $writer.Write([System.IO.File]::ReadAllBytes((Join-Path $brandDirectory "Minvex_icon_$size.png")))
    }
} finally {
    $writer.Dispose()
}
