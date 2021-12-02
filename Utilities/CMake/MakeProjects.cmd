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
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTAEF=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DUNITTEST=TRUE -DTE=TRUE
if %ERRORLEVEL% NEQ 0 goto done
call %ROOT_FOLDER%\Utilities\CMake\scripts\RunCMake.cmd -DGDK=TRUE
if %ERRORLEVEL% NEQ 0 goto done

%ROOT_FOLDER%\Utilities\CMake\ProjectFileProcessor\bin\Debug\ProjectFileProcessor.exe %ROOT_FOLDER%

goto done
:help
echo.
echo MakeProjects.cmd rootFolder [skipCopy]
echo.
echo Example:
echo MakeProjects.cmd C:\git\forks\libHttpClient
echo.

:done
