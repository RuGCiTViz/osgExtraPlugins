@echo off
cd /D "%~dp0"
IF NOT DEFINED PROJECT (
  for %%i in ("%CD%") do (
    SET PROJECT=%%~ni
	"%~0"
  )
  goto :eof
)
SETLOCAL ENABLEEXTENSIONS

echo PROJECT=%PROJECT%

call ..\osgversion.bat
IF NOT DEFINED MSVCV SET MSVCV=17_14
IF NOT DEFINED TP SET TP=x64


REM osgdev NEEDS TO SET

REM OSG_BUILD=E:\osg\36\laurens\vc17_14x64
REM OSG_OP_OT=E:\osg\36\laurens
REM OSG_ROOT=E:\osg\36\laurens\OpenSceneGraph

REM MAYBE
rem OSG_DIR=E:\osg\36\laurens\vc17_14x64

call ..\..\..\osgdev.bat

REM if exist src\icons\revision.bat CALL src\icons\revision.bat
REM cd /D "%~dp0"

echo on

REM set PREDEV=-DCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release
rem set PREDEV=-DCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release
set PREDEV=-DCMAKE_CONFIGURATION_TYPES:STRING=Debug;RelWithDebInfo
echo set PREDEV=%PREDEV% -C "%~dp0initial-cache.cmake"
set PREDEV=%PREDEV% -C "%~dp0initial-cache.cmake"

REM for local build
REM 
SET PROJ_BUILD_FOLDER=%CD%_vc%MSVCV%%TP%

rem DELETE OUTPUT FOLDER!
IF EXIST %PROJ_BUILD_FOLDER% (
rmdir /s /q %PROJ_BUILD_FOLDER%
)
IF EXIST %PROJ_BUILD_FOLDER% (
ECHO failed rmdir /s /q %PROJ_BUILD_FOLDER%
pause
GOTO :EOF
)


IF EXIST %PROJ_BUILD_FOLDER% (
  cd /D %PROJ_BUILD_FOLDER%
  GOTO MAKEBAT
) else (
mkdir %PROJ_BUILD_FOLDER%
cd /D %PROJ_BUILD_FOLDER%

REM type %~dp0\iCMakeCache.txt >> CMakeCache.txt
REM expand env variables:
echo SET PROJ_SOURCE_FOLDER=%~dp0|tr \\ />unixStylepwd.bat
CALL unixStylepwd.bat
del unixStylepwd.bat
echo SET UOSG_OP_OT=%OSG_OP_OT%|tr \\ />unixStylepwd.bat
CALL unixStylepwd.bat
del unixStylepwd.bat

REM set osg_ROOT=

@FOR /F "delims=" %%I IN (%~dp0\iCMakeCache.txt) DO echo ECHO %%I >> iCMakeCache.bat
ECHO OFF
REM CALL iCMakeCache.bat >> CMakeCache.txt

if "%TP%"=="Win32" (
	%CMAKE_EXE% -G %CMAKE_GEN% -T v110_xp %PREDEV% ../%PROJECT%
) else (
	if "%TP%"=="x64" (
		%CMAKE_EXE% -G %CMAKE_GEN% %PREDEV% ../%PROJECT%
	) else (
		echo FATAL - unknown target platform %TP%
	)
)

)
rem 
GOTO SKIPGUI
REM now start the cmake gui 
REM 
echo %CMAKE_GUI% .
%CMAKE_GUI% .

:SKIPGUI
REM copy %~dp0\viewer.vcproj.HPCV6.laurens.user src\
for /r plugins %%i in (*.vcxproj) do (
  if exist "%~dp0\vs_debugSettings\%%~nxi.user" copy "%~dp0\vs_debugSettings\%%~nxi.user" "%%i.user"
)


:MAKEBAT
if NOT exist vc%PROJECT%.bat (
  echo cd /D "%%~dp0"  > vc%PROJECT%.bat
  echo SET TP=%TP%>> vc%PROJECT%.bat
  echo SET MSVCV=%MSVCV%>> vc%PROJECT%.bat
  echo CALL ..\osgversion.bat >> vc%PROJECT%.bat
  echo CALL ..\..\..\osgdev.bat >> vc%PROJECT%.bat
  echo start /B %%DEVENV%% %PROJECT%.sln >> vc%PROJECT%.bat
)
if NOT exist %PROJECT%Build.bat (
  echo cd /D "%%~dp0"  > %PROJECT%Build.bat
  echo SET TP=%TP%>> %PROJECT%Build.bat
  echo SET MSVCV=%MSVCV%>> %PROJECT%Build.bat
  echo CALL ..\osgversion.bat >> %PROJECT%Build.bat
  echo CALL ..\..\..\osgdev.bat >> %PROJECT%Build.bat
  echo start "osg%OPENSCENEGRAPH_SOVERSION%" /wait %DEVENV% %PROJECT%.sln /Build "RelWithDebInfo|x64" /Project ZERO_CHECK >> %PROJECT%Build.bat
  echo start "osg%OPENSCENEGRAPH_SOVERSION%" /wait %DEVENV% %PROJECT%.sln /Build "Debug|x64" /Project ALL_BUILD >> %PROJECT%Build.bat
  echo start "osg%OPENSCENEGRAPH_SOVERSION%" /wait %DEVENV% %PROJECT%.sln /Build "RelWithDebInfo|x64" /Project ALL_BUILD >> %PROJECT%Build.bat
)

cd /D "%~dp0"
ECHO %CD%_vc%MSVCV%%TP%\vc%PROJECT%.bat ^&^& del "%%~0" > start_lastconfigured.bat
ENDLOCAL
start_lastconfigured.bat
:END