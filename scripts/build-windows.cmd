@echo off
rem Configures and builds VoiceTwitch with MSVC + Ninja + vcpkg.
rem Usage: scripts\build-windows.cmd [release|debug]
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=release

if "%VCPKG_ROOT%"=="" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSINSTALL=%%i
)
if "%VSINSTALL%"=="" (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSINSTALL=%%i
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=%VSINSTALL%\VC\vcpkg

cmake --preset windows-%CONFIG% || exit /b 1
cmake --build --preset windows-%CONFIG% || exit /b 1
endlocal
