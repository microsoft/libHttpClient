SET XdkEditionFinal=%XDKEDITION%
SET MSBuildVersion=15.8
set DurangoXdkInstallPath=C:\Program Files (x86)\Microsoft Durango XDK\

robocopy /NJS /NJH /MT:16 /S /NP "%BUILD_STAGINGDIRECTORY%\sdk.buildtools\buildMachine\Durango" "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\Common7\IDE\VC\VCTargets\Platforms\Durango"

set