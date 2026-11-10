# Downloads the pinned esbuild binary (the TypeScript -> JavaScript bundler used for game scripts)
# into bin\esbuild\. Node is not needed. The npm tarball is checked against the SHA-256 below.
$ErrorActionPreference = "Stop"

$Version = "0.28.2"
$Sha256 = "7286c3611b6f1f4c4d9ec90adcbc478407ff0d28ead96567f361d53e67613e19"  # win32-x64
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Dest = Join-Path $Root "bin/esbuild"
$Exe = Join-Path $Dest "esbuild.exe"

if ((Test-Path $Exe) -and ((& $Exe --version) -eq $Version)) {
    Write-Host "esbuild $Version already installed: $Exe"
    exit 0
}

$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $Tmp | Out-Null
try {
    $Tgz = Join-Path $Tmp "esbuild.tgz"
    Write-Host "Downloading esbuild $Version (win32-x64) into bin\esbuild ..."
    Invoke-WebRequest "https://registry.npmjs.org/@esbuild/win32-x64/-/win32-x64-$Version.tgz" -OutFile $Tgz
    $Actual = (Get-FileHash $Tgz -Algorithm SHA256).Hash.ToLower()
    if ($Actual -ne $Sha256) { throw "esbuild checksum mismatch: expected $Sha256, got $Actual" }
    tar -xzf $Tgz -C $Tmp
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    Copy-Item (Join-Path $Tmp "package/esbuild.exe") $Exe -Force
    & $Exe --version
} finally {
    Remove-Item -Recurse -Force $Tmp
}
