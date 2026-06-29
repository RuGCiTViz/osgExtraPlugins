@echo off
cd /D "%~dp0"
IF NOT DEFINED PROJECT (
  for %%i in ("%CD%") do (
    SET PROJECT=%%~ni
	"%~0"
  )
  goto :eof
)
SETLOCAL ENABLEDELAYEDEXPANSION

echo PROJECT=%PROJECT%
SET DIRUP=..\
if exist %DIRUP%osgversion.bat (
  call %DIRUP%osgversion.bat
) else (
  SET OSG_VERSION_DOTTED=3.6.5
  set OSG_USE_STABLE=36L
  pushd %~dp0\..\..
  SET OSG_OP_OT=!CD!
  popd
  SET OPENTHREADS_SOVERSION=21
  SET OPENSCENEGRAPH_SOVERSION=162
)
IF NOT DEFINED MSVCV SET MSVCV=17_14
IF NOT DEFINED TP SET TP=x64


if not exist ..\..\%DIRUP%osgdev.bat (
  SET OSG_BUILD=!OSG_OP_OT!\vc%MSVCV%%TP%
  SET OSG_ROOT=!OSG_OP_OT!\OpenSceneGraph
  SET CMAKE_EXE="E:\osg\3rdparty\repo\tools\CMake\bin\cmake.exe"
  SET CMAKE_GEN="Visual Studio 17 2022" -A x64
  SET CMAKE_GUI="E:\osg\3rdparty\repo\tools\CMake\bin\cmake-gui.exe"
  SET DEVENV="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"
REM MAYBE
  set OSG_DIR=E:\osg\36\laurens\vc17_14x64
) else (
  call ..\..\%DIRUP%osgdev.bat
)

REM if exist src\icons\revision.bat CALL src\icons\revision.bat
REM cd /D "%~dp0"

echo set PREDEV=-C "%~dp0initial-cache.cmake"
set PREDEV=-C "%~dp0initial-cache.cmake"

REM for local build
REM 
SET PROJ_BUILD_FOLDER=%CD%_vc%MSVCV%%TP%

GOTO :KEEP_BUILD

rem DELETE OUTPUT FOLDER!
IF EXIST %PROJ_BUILD_FOLDER% (
rmdir /s /q %PROJ_BUILD_FOLDER%
)
IF EXIST %PROJ_BUILD_FOLDER% (
ECHO failed rmdir /s /q %PROJ_BUILD_FOLDER%
pause
GOTO :EOF
)

:KEEP_BUILD
IF EXIST %PROJ_BUILD_FOLDER% (
  cd /D %PROJ_BUILD_FOLDER%
  GOTO MAKEBAT
)

mkdir %PROJ_BUILD_FOLDER%
cd /D %PROJ_BUILD_FOLDER%

REM type %~dp0\iCMakeCache.txt >> CMakeCache.txt
REM expand env variables:
rem echo SET PROJ_SOURCE_FOLDER=%~dp0|tr \\ />unixStylepwd.bat
PUSHD %~dp0
echo SET PROJ_SOURCE_FOLDER=%CD:\=/%/>%PROJ_BUILD_FOLDER%\unixStylepwd.bat
POPD
CALL unixStylepwd.bat
del unixStylepwd.bat
echo SET UOSG_OP_OT=%OSG_OP_OT:\=/%>unixStylepwd.bat
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
ECHO %CD%_vc%MSVCV%%TP%\vc%PROJECT%.bat ^& del "%%~0" > start_lastconfigured.bat
ENDLOCAL
start_lastconfigured.bat
:END