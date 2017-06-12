@echo off
if "%1" EQU "" goto help
echo on
set ROOT_FOLDER=%1
set NEW_FOLDER=%ROOT_FOLDER%\Utilities\CMake\output
set OLD_FOLDER=%ROOT_FOLDER%\Build

call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd 
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DXDK=TRUE
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DWIN32=TRUE
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTAEF=TRUE
rem call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTE=TRUE

%ROOT_FOLDER%\Utilities\CMake\ProjectFileProcessor\bin\Debug\ProjectFileProcessor.exe %ROOT_FOLDER%

if "%2" EQU "skipCopy" goto skipCopy
copy %NEW_FOLDER%\libHttpClient.141.UWP.C.vcxproj* %OLD_FOLDER%\libHttpClient.141.UWP.C
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