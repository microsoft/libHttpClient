# Adapted from PlayFab.C Build/vcpkg/Bootstrap-Vcpkg.ps1 @ bbe5fbbb. Change: the default vcpkg root is ~/.tools/vcpkg (shared by every repo) instead of <repo>/.tools/vcpkg.
# Bootstraps the pinned vcpkg tool (see vcpkg-pin.json). Honors XBBL_VCPKG_ROOT if set.
[CmdletBinding()]
param(
    [string] $VcpkgRoot = $env:XBBL_VCPKG_ROOT
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$pin = Get-Content (Join-Path $PSScriptRoot "vcpkg-pin.json") -Raw | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($VcpkgRoot))
{
    $VcpkgRoot = Join-Path $HOME ".tools\vcpkg"
}

# GetFullPath would resolve a relative root against the process directory, which is not
# necessarily the caller's current location.
$VcpkgRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($VcpkgRoot)
$separator = [System.IO.Path]::DirectorySeparatorChar

# Trailing separators would defeat the path comparisons below.
if ($VcpkgRoot -ne [System.IO.Path]::GetPathRoot($VcpkgRoot))
{
    $VcpkgRoot = $VcpkgRoot.TrimEnd($separator)
}

# Git hooks and rebase --exec export these; they would redirect our commands at another repo.
# The shell twin drops them per call with env -u, but PowerShell has no equivalent and clearing
# them here would outlive a failure, so refuse to run instead.
$gitEnvironmentVariables = "GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE",
                           "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES"

$inheritedGitVariables = @($gitEnvironmentVariables | Where-Object { Test-Path "Env:$_" })
if ($inheritedGitVariables.Count -gt 0)
{
    throw "$($inheritedGitVariables -join ', ') would point Git at another repository; unset them and run this script again."
}

# Resolve git once: a caller-defined function or alias named 'git' would shadow the real one.
$gitExecutable = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source

function Invoke-Git
{
    param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)

    & $gitExecutable @Arguments
    if ($LASTEXITCODE -ne 0)
    {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

function Get-GitOutput
{
    param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try
    {
        $output = (& $gitExecutable @Arguments 2>$null)
        $script:LastGitExitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return ($output | Out-String).Trim()
}

# Git reports resolved paths, so compare against both spellings of the repo: a junction or
# symlink would otherwise let the vcpkg root slip past this check.
$repoPaths = @($repoRoot)
$repoTopLevel = Get-GitOutput "-C" $repoRoot "rev-parse" "--show-toplevel"
if ($LastGitExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($repoTopLevel))
{
    $repoPaths += [System.IO.Path]::GetFullPath($repoTopLevel)
}

# The steps below rewrite the remote and working tree, so keep the vcpkg root out of this repo.
$vcpkgPrefix = $VcpkgRoot.TrimEnd($separator) + $separator
foreach ($repoPath in $repoPaths)
{
    if ($repoPath -eq $VcpkgRoot -or $repoPath.StartsWith($vcpkgPrefix, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "XBBL_VCPKG_ROOT '$VcpkgRoot' contains this repository; point it at a directory outside the repo."
    }
}

$vcpkgParent = Split-Path $VcpkgRoot -Parent
New-Item -ItemType Directory -Force -Path $vcpkgParent | Out-Null

# A broken .git makes Git fall back to the enclosing repo, so require a checkout rooted here.
# A .git file instead of a directory belongs to a linked worktree or submodule owned elsewhere.
function Test-VcpkgCheckout
{
    param([string] $Path)

    if (-not (Test-Path -LiteralPath (Join-Path $Path ".git") -PathType Container))
    {
        return $false
    }

    $cdup = Get-GitOutput "-C" $Path "rev-parse" "--show-cdup"

    return ($LastGitExitCode -eq 0 -and $cdup -eq "")
}

# Rewriting the remote and checking out the pin would destroy an unrelated repository, so make
# sure the checkout really is vcpkg before adopting it.
function Test-VcpkgRepository
{
    param([string] $Path)

    # An interrupted bootstrap leaves a commit-less checkout; adopt it so the next run can resume.
    Get-GitOutput "-C" $Path "rev-parse" "--verify" "HEAD" | Out-Null
    if ($LastGitExitCode -ne 0)
    {
        return $true
    }

    $originUrl = Get-GitOutput "-C" $Path "remote" "get-url" "origin"
    if ($LastGitExitCode -eq 0 -and [string]::Equals($originUrl, $pin.repository, [System.StringComparison]::OrdinalIgnoreCase))
    {
        return $true
    }

    # A clone from a mirror or a fork has a different remote, so fall back to the vcpkg layout.
    return ((Test-Path -LiteralPath (Join-Path $Path "ports") -PathType Container) -and
            (Test-Path -LiteralPath (Join-Path $Path "triplets") -PathType Container))
}

$isDefaultRoot = $VcpkgRoot -eq [System.IO.Path]::GetFullPath((Join-Path $HOME ".tools\vcpkg"))

# Comparing spellings cannot see through a junction or symlink. Once the root is known to be a
# checkout rooted at itself, Git's toplevel names the root, so it can be compared with the repo.
if ((Test-VcpkgCheckout $VcpkgRoot) -and -not [string]::IsNullOrWhiteSpace($repoTopLevel))
{
    $vcpkgTopLevel = Get-GitOutput "-C" $VcpkgRoot "rev-parse" "--show-toplevel"
    if ($LastGitExitCode -eq 0 -and [string]::Equals($vcpkgTopLevel, $repoTopLevel, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "XBBL_VCPKG_ROOT '$VcpkgRoot' resolves to this repository; point it at a directory outside the repo."
    }
}

$unusableReason = $null
if (Test-Path -LiteralPath $VcpkgRoot)
{
    if (-not (Test-VcpkgCheckout $VcpkgRoot))
    {
        $unusableReason = "is not a Git checkout rooted at that path"
    }
    elseif (-not (Test-VcpkgRepository $VcpkgRoot))
    {
        $unusableReason = "is a Git checkout but does not look like vcpkg"
    }
}

if ($unusableReason)
{
    if (-not $isDefaultRoot)
    {
        throw "XBBL_VCPKG_ROOT '$VcpkgRoot' $unusableReason; point it at a vcpkg checkout or an empty directory."
    }

    # Deleting through a link would take out whatever it points at.
    if ((Get-Item -LiteralPath $VcpkgRoot -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)
    {
        throw "'$VcpkgRoot' is a link; remove it and run this script again."
    }

    Write-Host "'$VcpkgRoot' $unusableReason; recreating it."
    Remove-Item -LiteralPath $VcpkgRoot -Recurse -Force
}

if (-not (Test-Path -LiteralPath $VcpkgRoot))
{
    New-Item -ItemType Directory -Path $VcpkgRoot | Out-Null
    Invoke-Git -Arguments "-C", $VcpkgRoot, "init"
}

if (-not (Test-VcpkgCheckout $VcpkgRoot))
{
    throw "'$VcpkgRoot' is not a Git checkout rooted at that path; refusing to run Git commands that would affect the enclosing repository."
}

Get-GitOutput "-C" $VcpkgRoot "remote" "get-url" "origin" | Out-Null
if ($LastGitExitCode -eq 0)
{
    Invoke-Git -Arguments "-C", $VcpkgRoot, "remote", "set-url", "origin", $pin.repository
}
else
{
    Invoke-Git -Arguments "-C", $VcpkgRoot, "remote", "add", "origin", $pin.repository
}

$currentCommit = Get-GitOutput "-C" $VcpkgRoot "rev-parse" "--verify" "HEAD"

if ($LastGitExitCode -ne 0 -or $currentCommit -ne $pin.commit)
{
    Invoke-Git -Arguments "-C", $VcpkgRoot, "fetch", "--depth", "1", "origin", $pin.commit

    # An interrupted checkout leaves this lock behind, and Git then refuses to touch the index.
    $indexLock = Join-Path $VcpkgRoot ".git/index.lock"
    if (Test-Path -LiteralPath $indexLock)
    {
        Write-Host "Removing leftover '$indexLock'."
        Remove-Item -LiteralPath $indexLock -Force
    }

    Invoke-Git -Arguments "-C", $VcpkgRoot, "checkout", "--force", "--detach", "FETCH_HEAD"
}

$currentCommit = Get-GitOutput "-C" $VcpkgRoot "rev-parse" "HEAD"
if ($currentCommit -ne $pin.commit)
{
    throw "Expected vcpkg commit '$($pin.commit)', but found '$currentCommit'."
}

$isWindowsHost = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$vcpkgExecutable = Join-Path $VcpkgRoot $(if ($isWindowsHost) { "vcpkg.exe" } else { "vcpkg" })

# The checkout may have moved to a new pin while keeping a vcpkg binary built for the old one, so
# compare the binary against the tool release the checked-out scripts expect.
function Get-ExpectedToolTag
{
    $metadataPath = Join-Path $VcpkgRoot "scripts/vcpkg-tool-metadata.txt"
    if (-not (Test-Path -LiteralPath $metadataPath))
    {
        return $null
    }

    foreach ($line in Get-Content -LiteralPath $metadataPath)
    {
        if ($line -match '^\s*VCPKG_TOOL_RELEASE_TAG\s*=\s*(\S+)\s*$')
        {
            return $Matches[1]
        }
    }

    return $null
}

function Test-VcpkgToolCurrent([string] $ExpectedTag)
{
    if (-not (Test-Path -LiteralPath $vcpkgExecutable))
    {
        return $false
    }

    if (-not $ExpectedTag)
    {
        return $true
    }

    try
    {
        $versionOutput = & $vcpkgExecutable version 2>$null | Out-String
    }
    catch
    {
        return $false
    }

    if ($LASTEXITCODE -ne 0)
    {
        return $false
    }

    return $versionOutput -match ("version\s+" + [regex]::Escape($ExpectedTag) + "(-|\s|$)")
}

$expectedToolTag = Get-ExpectedToolTag

if (-not (Test-VcpkgToolCurrent $expectedToolTag))
{
    if (Test-Path -LiteralPath $vcpkgExecutable)
    {
        Write-Host "vcpkg tool at '$vcpkgExecutable' does not match expected release '$expectedToolTag'; re-bootstrapping."
    }

    if ($isWindowsHost)
    {
        & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    }
    else
    {
        & (Join-Path $VcpkgRoot "bootstrap-vcpkg.sh") -disableMetrics
    }

    if ($LASTEXITCODE -ne 0)
    {
        throw "vcpkg bootstrap failed with exit code $LASTEXITCODE."
    }

    if (-not (Test-VcpkgToolCurrent $expectedToolTag))
    {
        throw "vcpkg bootstrap did not produce tool release '$expectedToolTag' at '$vcpkgExecutable'."
    }
}

$env:XBBL_VCPKG_ROOT = $VcpkgRoot
Write-Host "vcpkg $($pin.tag) is ready at '$VcpkgRoot' ($currentCommit)."
