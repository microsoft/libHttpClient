if "%1" EQU "" goto help
set ROOT_FOLDER=%1
set NEW_FOLDER=%ROOT_FOLDER%\Utilities\CMake\output
set OLD_FOLDER=%ROOT_FOLDER%\Build

fc %OLD_FOLDER%\libHttpClient.141.UWP.C\libHttpClient.141.UWP.C.vcxproj %NEW_FOLDER%\libHttpClient.141.UWP.C.vcxproj
if %ERRORLEVEL% NEQ 0 goto email

goto done

:email
if "%2" NEQ "emailfailures" goto done
set MSGTITLE="REGEN BUILD FILES: %BUILD_SOURCEVERSIONAUTHOR% %BUILD_DEFINITIONNAME% %BUILD_SOURCEBRANCH% = %agent.jobstatus%"
set MSGBODY="%TFS_DROPLOCATION%    https://microsoft.visualstudio.com/OS/_build/index?buildId=%BUILD_BUILDID%&_a=summary"
call \\scratch2\scratch\jasonsa\tools\send-build-email.cmd %MSGTITLE% %MSGBODY% 

goto done
:help
@echo off
echo.
echo CompareBuildFiles.cmd rootFolder [emailfailures]
echo.
echo Example:
echo CompareBuildFiles.cmd C:\git\forks\xbox-live-api
echo.

:done
