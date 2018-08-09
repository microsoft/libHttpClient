@echo off
echo on
set ROOT_FOLDER=%~dp0\..\..
rem force root folder to an absolute path
pushd %ROOT_FOLDER%
set ROOT_FOLDER=%CD%
popd
set NEW_FOLDER=%ROOT_FOLDER%\Utilities\CMake\output
set OLD_FOLDER=%ROOT_FOLDER%\Build

call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUWP=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DXDK=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DBUILDWIN32=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DBUILDANDROID=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTAEF=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTE=TRUE
if %ERRORLEVEL% NEQ 0 goto done

%ROOT_FOLDER%\Utilities\CMake\ProjectFileProcessor\bin\Debug\ProjectFileProcessor.exe %ROOT_FOLDER%

if "%2" EQU "skipCopy" goto skipCopy
rem copy %NEW_FOLDER%\libHttpClient.110.XDK.C.vcxproj* %OLD_FOLDER%\libHttpClient.110.XDK.C
rem copy %NEW_FOLDER%\libHttpClient.110.XDK.WinRT.vcxproj* %OLD_FOLDER%\libHttpClient.110.XDK.WinRT
copy %NEW_FOLDER%\libHttpClient.140.UWP.C.vcxproj* %OLD_FOLDER%\libHttpClient.140.UWP.C
copy %NEW_FOLDER%\libHttpClient.140.UWP.WinRT.vcxproj* %OLD_FOLDER%\libHttpClient.140.UWP.WinRT
copy %NEW_FOLDER%\libHttpClient.140.XDK.C.vcxproj* %OLD_FOLDER%\libHttpClient.140.XDK.C
copy %NEW_FOLDER%\libHttpClient.140.Win32.C.vcxproj* %OLD_FOLDER%\libHttpClient.140.Win32.C
copy %NEW_FOLDER%\libHttpClient.141.UWP.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.UWP.C
copy %NEW_FOLDER%\libHttpClient.141.XDK.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.XDK.C
copy %NEW_FOLDER%\libHttpClient.141.Win32.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.Win32.C
copy %NEW_FOLDER%\libHttpClient.141.Android.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.Android.C
copy %NEW_FOLDER%\libHttpClient.UnitTest.141.TAEF.vcxproj* %OLD_FOLDER%\libHttpClient.UnitTest.141.TAEF
copy %NEW_FOLDER%\libHttpClient.UnitTest.141.TE.vcxproj* %OLD_FOLDER%\libHttpClient.UnitTest.141.TE

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
