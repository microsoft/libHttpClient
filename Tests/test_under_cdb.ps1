param(
	[string]$TestExe = "C:\Program Files (x86)\Windows Kits\10\Testing\Runtimes\TAEF\x64\TE.exe",
	[string[]]$TestArgs = @("${PSScriptRoot}\..\Out\x64\Debug\libHttpClient.UnitTest.TAEF\libHttpClient.UnitTest.TAEF.dll"),
	[string]$TestName = "",
	[switch]$InProc,
	[int]$MaxIterations = 0,
	[string]$LogDir = "${PSScriptRoot}\..\out\cdb-dumps",
	[switch]$EnablePageHeap,
	[switch]$ManualDebug
)

$realExe = (Get-Item $TestExe).FullName
$exeLeaf = Split-Path -Leaf $realExe

$ErrorActionPreference = "Stop"

# Resolve tool paths
$cdbCmd = Get-Command cdb.exe -ErrorAction SilentlyContinue
$gflagsCmd = Get-Command gflags.exe -ErrorAction SilentlyContinue
$cdb = if ($cdbCmd) { $cdbCmd.Source } else { $null }
$gflags = if ($gflagsCmd) { $gflagsCmd.Source } else { $null }

if (-not $cdb) {
	Write-Host "cdb.exe not found. Install Windows SDK Debugging Tools or add to PATH." -ForegroundColor Yellow
	exit 1
}

if (-not (Test-Path $realExe)) {
	Write-Host "TestExe not found: $realExe" -ForegroundColor Yellow
	Write-Host "Use -TestExe to point at tests.exe." -ForegroundColor Yellow
	exit 1
}

if ($TestArgs.Count -eq 1 -and ($TestArgs[0] -match "\s")) {
	$TestArgs = [regex]::Matches($TestArgs[0], '"(?:\\"|[^"])*"|\S+') | ForEach-Object { $_.Value }
}

# Strip surrounding quotes from tokens
$TestArgs = $TestArgs | ForEach-Object {
	if ($_ -match '^".*"$') { $_.Substring(1, $_.Length - 2) } else { $_ }
}

if ($TestName) {
	$TestArgs += "/name:$TestName"
}

if ($InProc) {
	$TestArgs += "/inproc"
}

if ($EnablePageHeap) {
	if (-not $gflags) {
		Write-Host "gflags.exe not found. Install Windows SDK Debugging Tools or add to PATH." -ForegroundColor Yellow
		exit 1
	}
	$targets = @($exeLeaf, "te.processhost.exe")
	foreach ($target in $targets) {
		& $gflags /p /enable $target /full | Out-Null
		Write-Host "PageHeap enabled for $target" -ForegroundColor Green
	}
}

# Ensure log/dump dir exists
$null = New-Item -ItemType Directory -Path $LogDir -Force

function New-DumpOnSymbol {
	param([string]$Symbol, [string]$DumpPath)
	return ('bu ' + $Symbol + ' "!analyze -v; ~*k; .dump /ma /o ' + $DumpPath + '; .kill; q"')
}

Write-Host "Using TestExe: $realExe" -ForegroundColor DarkGray
Write-Host "Using TestArgs: $($TestArgs -join ' ')" -ForegroundColor DarkGray

$iteration = 0
while ($true) {
	$iteration++
	if ($MaxIterations -gt 0 -and $iteration -gt $MaxIterations) {
		Write-Host "Reached MaxIterations=$MaxIterations. Exiting." -ForegroundColor Cyan
		break
	}

	$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
	$nonce = [guid]::NewGuid().ToString("N")
	$logPath = Join-Path $LogDir ("cdb_{0:D6}_{1}_{2}.log" -f $iteration, $stamp, $nonce)
	$dumpPath = Join-Path $LogDir ("crash_{0:D6}_{1}_{2}.dmp" -f $iteration, $stamp, $nonce)
	$cmdFile = Join-Path $LogDir ("cdb_{0:D6}_{1}_{2}.cmd" -f $iteration, $stamp, $nonce)

	Write-Host "Iteration $iteration" -ForegroundColor Cyan

	if (Test-Path $dumpPath) { Remove-Item $dumpPath -Force -ErrorAction SilentlyContinue }

	$dumpPathCdb = $dumpPath -replace '\\','/'
	$stackTraceFile = Join-Path $LogDir ("stack_{0:D6}_{1}_{2}.txt" -f $iteration, $stamp, $nonce)
	$stackPathCdb = $stackTraceFile -replace '\\','/'

	# Helper function to generate diagnostic commands (log stacks, dump, exit)
	function Get-DiagnosticCommand {
		param(
			[string]$StackFile,
			[string]$DumpFile,
			[switch]$IncludeAnalyze
		)
		$analyze = if ($IncludeAnalyze) { '!analyze -v; ' } else { '' }
		return ".logopen $StackFile; ${analyze}~*k; .logclose; .dump /ma /o $DumpFile; q"
	}

	$diagCmd = Get-DiagnosticCommand -StackFile $stackPathCdb -DumpFile $dumpPathCdb -IncludeAnalyze
	$diagCmdNoAnalyze = Get-DiagnosticCommand -StackFile $stackPathCdb -DumpFile $dumpPathCdb

	# Ctrl+C handler: capture all thread stacks and dump before quitting
	$ctrlCHandler = 'sxe -c "' + $diagCmd + '" 0x40010005'
	# Access violation handler: capture stacks and dump
	$avHandler = 'sxe -c "' + $diagCmd + '" av'
	# Stack overflow handler: capture stacks and dump
	$stackOverflowHandler = 'sxe -c "' + $diagCmd + '" 0xc0000409'

	# Common debugger setup
	$cmdLines = @(
		'.childdbg 1'
		'sxd ibp'
		'.logopen ' + $logPath
		('bp ucrtbased!abort "' + $diagCmd + '"')
		('bp ucrtbase!abort "' + $diagCmd + '"')
		('bp vcruntime140d!_invoke_watson "' + $diagCmd + '"')
		$stackOverflowHandler
		$avHandler
		'sxd c0000374'
		$ctrlCHandler
		'g'
	)

	# Append mode-specific exit behavior
	if ($ManualDebug) {
		$cmdLines += 'g'
	} else {
		$cmdLines += 'q'
	}
	$cmdLines | Set-Content -Path $cmdFile -Encoding ASCII

	& $cdb -o -logo $logPath -cf $cmdFile $realExe @TestArgs
	$exitCode = $LASTEXITCODE

    if (Test-Path -Path $dumpPath -PathType Leaf ) {
		Write-Host "Crash dump detected; stopping" -ForegroundColor Red
		Write-Host "Log: $logPath"
		Write-Host "Stack trace: $stackTraceFile"
		Write-Host "Dump: $dumpPath"
		break

    }

	if ($exitCode -ne 0) {
		Write-Host "Non-zero exit code: $exitCode" -ForegroundColor Red
		Write-Host "Log: $logPath"
		Write-Host "Stack trace: $stackTraceFile"
		Write-Host "Dump: $dumpPath"
		break
	}

	# Successful run: clean up log/cmd to avoid accumulation (keep dumps/stacks for retention)
	if (Test-Path $logPath) { Remove-Item $logPath -Force -ErrorAction SilentlyContinue }
	if (Test-Path $cmdFile) { Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue }
}

if ($EnablePageHeap -and $gflags) {
	$targets = @($exeLeaf, "te.processhost.exe")
	foreach ($target in $targets) {
		& $gflags /p /disable $target | Out-Null
		Write-Host "PageHeap disabled for $target" -ForegroundColor Green
	}
}
