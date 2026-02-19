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
	$dumpOnBreakpoint = 'sxd ibp'
	$dumpSymbols = @(
		'TE_Common!WEX::TestExecution::Verify::VerificationFailed<tagVARIANT,tagVARIANT,WEX::TestExecution::Verify::VerifyMessageFunctor>',
		'TE_Common!WEX::TestExecution::Verify::VerificationFailed<tagVARIANT,tagVARIANT,WEX::TestExecution::Private::VerifyMessageFunctor>',
		'TE_Common!WEX::TestExecution::ComVerify::AreEqual',
		'libHttpClient_UnitTest_TAEF!WEX::TestExecution::Private::MacroVerify::VerificationFailed',
		'libHttpClient_UnitTest_TAEF!WEX::TestExecution::Private::MacroVerify::AreEqualImpl<unsigned int>',
		'libHttpClient_UnitTest_TAEF!WEX::TestExecution::Private::MacroVerify::IsTrueImpl<char>',
		'ucrtbased!abort',
		'ucrtbase!abort',
		'ucrtbased!issue_debug_notification',
		'ucrtbase!issue_debug_notification',
		'vcruntime140d!__fastfail',
		'vcruntime140_1d!__fastfail',
		'vcruntime140d!_invoke_watson',
		'vcruntime140_1d!_invoke_watson',
		'ucrtbased!_CrtDbgReportW',
		'ucrtbased!_raiseabort',
		'ucrtbase!_raiseabort'
	)
	$dumpBreakpoints = $dumpSymbols | ForEach-Object { New-DumpOnSymbol -Symbol $_ -DumpPath $dumpPathCdb }

	if ($ManualDebug) {
		$cmdLines = @(
			'.childdbg 1',
			$dumpOnBreakpoint
		) + $dumpBreakpoints + @(
			'sxd eh',
			'sxi 0xe06d7363',
			'sxe 0x40000015',
			'sxe 0xc0000409',
			'sxe 0xc000013a',
			'sxe av',
			'sxe c0000374',
			'g',
			'g'
		)
	} else {
		$cmdLines = @(
			'.childdbg 1',
			$dumpOnBreakpoint
		) + $dumpBreakpoints + @(
			'sxd eh',
			'sxi 0xe06d7363',
			('sxe -c "!analyze -v; ~*k; .dump /ma /o ' + $dumpPathCdb + '; .kill; q" 0xc000013a'),
			('sxe -c "!analyze -v; ~*k; .dump /ma /o ' + $dumpPathCdb + '; .kill; q" 0x40000015'),
			('sxe -c "!analyze -v; ~*k; .dump /ma /o ' + $dumpPathCdb + '; .kill; q" 0xc0000409'),
			('sxd -c "!analyze -v; ~*k; .dump /ma /o ' + $dumpPathCdb + '; .kill; q" av'),
			('sxd -c "!analyze -v; ~*k; .dump /ma /o ' + $dumpPathCdb + '; .kill; q" c0000374'),
			'g',
			'g', # resume on TE's initial breakpoint
			'q'
		)
	}
	$cmdLines | Set-Content -Path $cmdFile -Encoding ASCII

	& $cdb -o -logo $logPath -cf $cmdFile $realExe @TestArgs
	$exitCode = $LASTEXITCODE

    if (Test-Path -Path $dumpPath -PathType Leaf ) {
		Write-Host "Crash dump detected; stopping" -ForegroundColor Red
		Write-Host "Log: $logPath"
		Write-Host "Dump: $dumpPath"
		break

    }

	if ($exitCode -ne 0) {
		Write-Host "Non-zero exit code: $exitCode" -ForegroundColor Red
		Write-Host "Log: $logPath"
		Write-Host "Dump: $dumpPath"
		break
	}

	# Successful run: clean up log/dump/cmd to avoid accumulation
	if (Test-Path $logPath) { Remove-Item $logPath -Force -ErrorAction SilentlyContinue }
	if (Test-Path $dumpPath) { Remove-Item $dumpPath -Force -ErrorAction SilentlyContinue }
	if (Test-Path $cmdFile) { Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue }
}

if ($EnablePageHeap -and $gflags) {
	$targets = @($exeLeaf, "te.processhost.exe")
	foreach ($target in $targets) {
		& $gflags /p /disable $target | Out-Null
		Write-Host "PageHeap disabled for $target" -ForegroundColor Green
	}
}
