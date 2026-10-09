# Restores the third-party sources libHttpClient builds from (asio, boost-wintls, curl, openssl,
# websocketpp, zlib) into External/<name>, at the pins in ports/libhttpclient-deps/deps.cmake.
#
# Builds run this automatically (MSBuild: Build/libHttpClient.vcpkg.props; CMake:
# Build/vcpkg/AutoRestore.cmake), so you normally never call it yourself. It is a fast no-op when
# External/ is already up to date.
#
# By default sources are downloaded from GitHub; no Microsoft-internal access is needed.
#   -BlockOrigin (or HC_VCPKG_BLOCK_ORIGIN=1): fetch only from Microsoft's Terrapin mirror. Internal CI.
#   -Force: restore even if External/ looks up to date.
# Set HC_SKIP_VCPKG_RESTORE=1 to disable the automatic restore in builds.
[CmdletBinding()]
param(
    [switch] $BlockOrigin,
    [switch] $Force,
    [string] $VcpkgRoot = $env:XBBL_VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$depsFile = Join-Path $PSScriptRoot "ports\libhttpclient-deps\deps.cmake"
$stampFile = Join-Path $repoRoot "External\.vcpkg-deps.stamp"
$terrapin = "https://vcpkg.storage.devpackages.microsoft.io/artifacts/"

if ($env:HC_VCPKG_BLOCK_ORIGIN -eq "1") { $BlockOrigin = $true }

$depsText = [System.IO.File]::ReadAllText($depsFile)
$depPaths = @([regex]::Matches($depsText, '(?m)^\s*set\(HC_DEP_\S+_PATH\s+(\S+)\)') | ForEach-Object { $_.Groups[1].Value })
if ($depPaths.Count -eq 0) { throw "No HC_DEP_*_PATH entries found in '$depsFile'." }

function Test-UpToDate
{
    if (-not (Test-Path -LiteralPath $stampFile)) { return $false }
    if ([System.IO.File]::ReadAllText($stampFile) -cne $depsText) { return $false }
    foreach ($p in $depPaths)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $p))) { return $false }
    }
    return $true
}

# Parallel builds (several projects importing the same props) can call this concurrently.
$sha = [System.Security.Cryptography.SHA256]::Create()
$key = [System.BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($repoRoot.ToLowerInvariant()))).Replace("-", "").Substring(0, 16)
$mutex = New-Object System.Threading.Mutex($false, "Local\libHttpClient-vcpkg-restore-$key")
try { [void]$mutex.WaitOne() } catch [System.Threading.AbandonedMutexException] { }

try
{
    if (-not $Force -and (Test-UpToDate))
    {
        Write-Host "libHttpClient third-party sources are up to date."
        exit 0
    }

    Write-Host "Restoring libHttpClient third-party sources ($($depPaths -join ', '))..."

    & (Join-Path $PSScriptRoot "Bootstrap-Vcpkg.ps1") -VcpkgRoot $VcpkgRoot
    $VcpkgRoot = $env:XBBL_VCPKG_ROOT
    $vcpkg = Join-Path $VcpkgRoot "vcpkg.exe"

    $installRoot = Join-Path $PSScriptRoot "vcpkg_installed"
    $vcpkgArgs = @("install", "--x-manifest-root=$PSScriptRoot", "--x-install-root=$installRoot")
    if ($BlockOrigin)
    {
        $vcpkgArgs += "--x-asset-sources=clear;x-azurl,$terrapin;x-block-origin"
    }

    & $vcpkg @vcpkgArgs
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed with exit code $LASTEXITCODE." }

    $share = @(Get-ChildItem -Path $installRoot -Directory |
               ForEach-Object { Join-Path $_.FullName "share\libhttpclient-deps\src" } |
               Where-Object { Test-Path -LiteralPath $_ })
    if ($share.Count -ne 1) { throw "Expected one restored libhttpclient-deps tree under '$installRoot', found $($share.Count)." }

    foreach ($p in $depPaths)
    {
        $from = Join-Path $share[0] $p
        $to = Join-Path $repoRoot $p
        if (-not (Test-Path -LiteralPath $from)) { throw "Restored tree is missing '$p'." }

        # A leftover git submodule checkout (from before the move to vcpkg) is replaced as well.
        & robocopy.exe $from $to /MIR /NFL /NDL /NJH /NJS /NP /MT:16 | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Copying '$p' failed (robocopy exit code $LASTEXITCODE)." }
    }

    [System.IO.File]::WriteAllText($stampFile, $depsText)
    Write-Host "libHttpClient third-party sources restored under '$(Join-Path $repoRoot 'External')'."
    exit 0
}
finally
{
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
