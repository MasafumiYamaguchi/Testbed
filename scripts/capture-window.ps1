param([Parameter(Mandatory=$true)][string]$Executable, [string]$OutputDirectory = "evidence")
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out = (Resolve-Path $OutputDirectory).Path
$exe = (Resolve-Path $Executable).Path
# Keep the process alive while PowerShell compiles the native capture helper.
$process = Start-Process -FilePath $exe -ArgumentList @("--frames", "600", "--self-test", "--lifecycle-test", "--capture", "client.bmp") -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/app.stdout.log" -RedirectStandardError "$out/app.stderr.log"
try {
    $ready = $false
    for ($i = 0; $i -lt 600; $i++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw "App exited before capture: $($process.ExitCode)" }
        if ($process.MainWindowHandle -ne 0 -and (Test-Path "$out/client.bmp")) { $ready = $true; break }
    }
    if (!$ready) { throw "App window/framebuffer did not appear within sixty seconds" }
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public class WindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
}
'@
    [WindowCapture]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 500
    $rect = New-Object WindowCapture+Rect
    if (![WindowCapture]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) { throw "GetWindowRect failed" }
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right-$rect.Left), ($rect.Bottom-$rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save("$out/window.png", [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
    $client = [System.Drawing.Image]::FromFile("$out/client.bmp")
    try { $client.Save("$out/framebuffer.png", [System.Drawing.Imaging.ImageFormat]::Png) } finally { $client.Dispose() }
    $memory = @()
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if (!$process.HasExited) {
            $memory += [pscustomobject]@{Utc=[DateTime]::UtcNow.ToString("o"); PrivateBytes=$process.PrivateMemorySize64; WorkingSet=$process.WorkingSet64; Handles=$process.HandleCount}
        }
        Start-Sleep -Milliseconds 250
    }
    $memory | Export-Csv "$out/process-memory.csv" -NoTypeInformation
    if (!$process.HasExited) { throw "App did not exit within thirty seconds" }
    if ($process.ExitCode -ne 0) { throw "App failed with exit code $($process.ExitCode)" }
    # Independent launches cover shutdown/reinitialization and preserve all five
    # shape cases; each PNG comes from the GPU target, not a generated mockup.
    foreach ($scene in 0..4) {
        $p = Start-Process -FilePath $exe -ArgumentList @("--frames", "90", "--scene", "$scene", "--capture", "scene-$scene.bmp") -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/scene-$scene.stdout.log" -RedirectStandardError "$out/scene-$scene.stderr.log"
        if (!$p.WaitForExit(60000)) { Stop-Process -Id $p.Id -Force; throw "Scene $scene timed out" }
        if ($p.ExitCode -ne 0) { throw "Scene $scene failed: $($p.ExitCode)" }
        $img = [System.Drawing.Image]::FromFile("$out/scene-$scene.bmp")
        try { $img.Save("$out/scene-$scene.png", [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
    }
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
}
