param([string]$Destination = "tools/dxc")
$ErrorActionPreference = "Stop"
$url = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/dxc_2025_07_14.zip"
$expected = "9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4"
$zip = Join-Path ([IO.Path]::GetTempPath()) ("white-dxc-" + [guid]::NewGuid() + ".zip")
try {
    Invoke-WebRequest $url -OutFile $zip
    if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "DXC archive checksum mismatch" }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    Expand-Archive $zip -DestinationPath $Destination -Force
} finally { Remove-Item $zip -ErrorAction SilentlyContinue }
