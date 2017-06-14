@echo off
if "%1" EQU "" goto help
echo on
set ROOT_FOLDER=%1
if "%1" EQU "1" set ROOT_FOLDER=C:\git\forks\libHttpClient
set NEW_FOLDER=%ROOT_FOLDER%\Utilities\CMake\output
set OLD_FOLDER=%ROOT_FOLDER%\Build

call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUWP=TRUE
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DXDK=TRUE
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DWIN32=TRUE
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTAEF=TRUE
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTE=TRUE

%ROOT_FOLDER%\Utilities\CMake\ProjectFileProcessor\bin\Debug\ProjectFileProcessor.exe %ROOT_FOLDER%

if "%2" EQU "skipCopy" goto skipCopy
copy %NEW_FOLDER%\libHttpClient.110.XDK.C.vcxproj* %OLD_FOLDER%\libHttpClient.110.XDK.C
copy %NEW_FOLDER%\libHttpClient.110.XDK.WinRT.vcxproj* %OLD_FOLDER%\libHttpClient.110.XDK.WinRT
copy %NEW_FOLDER%\libHttpClient.140.UWP.C.vcxproj* %OLD_FOLDER%\libHttpClient.140.UWP.C
copy %NEW_FOLDER%\libHttpClient.140.UWP.WinRT.vcxproj* %OLD_FOLDER%\libHttpClient.140.UWP.WinRT
copy %NEW_FOLDER%\libHttpClient.140.XDK.C.vcxproj* %OLD_FOLDER%\libHttpClient.140.XDK.C
copy %NEW_FOLDER%\libHttpClient.141.UWP.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.UWP.C
copy %NEW_FOLDER%\libHttpClient.141.XDK.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.XDK.C
copy %NEW_FOLDER%\libHttpClient.UnitTest.140.TAEF.vcxproj* %OLD_FOLDER%\libHttpClient.UnitTest.140.TAEF
copy %NEW_FOLDER%\libHttpClient.UnitTest.140.TE.vcxproj* %OLD_FOLDER%\libHttpClient.UnitTest.140.TE

:skipCopy

goto done
:help
echo.
echo MakeProjects.cmd rootFolder [skipCopy]
echo.
echo Example:
echo MakeProjects.cmd C:\git\forks\libHttpClient
echo.

:done