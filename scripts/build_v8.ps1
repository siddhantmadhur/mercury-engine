# Builds the pinned V8 (third_party/v8/VERSION) as a static monolith library on Windows.
# Takes ~30-60 min and ~15 GB of disk. Idempotent: finished steps are skipped.
# Requires Visual Studio 2022 with the Windows SDK and Git.
$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$V8Root = Join-Path $Root "third_party/v8"
$Version = (Get-Content (Join-Path $V8Root "VERSION")).Trim()
$Depot = Join-Path $V8Root "depot_tools"
$Out = Join-Path $V8Root "v8/out/release"
$Lib = Join-Path $Out "obj/v8_monolith.lib"
$Stamp = Join-Path $V8Root ".built_version"

if ((Test-Path $Lib) -and (Test-Path $Stamp) -and ((Get-Content $Stamp).Trim() -eq $Version)) {
    Write-Host "V8 $Version already built: $Lib"
    exit 0
}

if (-not (Test-Path $Depot)) {
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git $Depot
}
$env:PATH = "$Depot;$env:PATH"
$env:DEPOT_TOOLS_UPDATE = "0"
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"

Set-Location $V8Root
if (-not (Test-Path "v8")) { fetch --no-history v8 }
Set-Location v8
git fetch --depth 1 origin "refs/tags/${Version}:refs/tags/${Version}" 2>$null
git checkout $Version
gclient sync -D --no-history

$Args = ((Get-Content (Join-Path $V8Root "args.gn")) | Where-Object { $_.Trim() -ne "" }) -join "`n"
gn gen $Out --args="$Args`ntarget_cpu=`"x64`""
ninja -C $Out v8_monolith
Set-Content $Stamp $Version
Write-Host "Built $Lib"
