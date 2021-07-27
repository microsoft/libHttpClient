echo Running fetchTools.cmd

rem Log build machine state
if EXIST "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\amd64\msbuild.exe" "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\MSBuild\15.0\Bin\amd64\msbuild.exe" /version
if EXIST "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe" "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe" /version
dir /b "C:\Program Files (x86)\Microsoft SDKs"
dir /b "C:\Program Files (x86)\Windows Kits\10\Include"
set

set patArg=%1
set toolsUrl=%2
cd /D %BUILD_STAGINGDIRECTORY%
call git clone https://anything:%patArg%@%toolsUrl%
cd sdk.buildtools
git reset --hard HEAD
if "%buildToolsBranchArg%" NEQ "" call git checkout %buildToolsBranchArg%
cd /D %BUILD_STAGINGDIRECTORY%
dir "%BUILD_STAGINGDIRECTORY%\sdk.buildtools\buildMachine

