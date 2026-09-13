@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where cmake >nul 2>nul || (echo [ERROR] CMake is required. & exit /b 1)
where cpack >nul 2>nul || (echo [ERROR] CPack is required and is installed with CMake. & exit /b 1)
where makensis >nul 2>nul
if errorlevel 1 (
  if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "PATH=%ProgramFiles(x86)%\NSIS;%PATH%"
)
where makensis >nul 2>nul
if errorlevel 1 (
  if exist "%ProgramFiles%\NSIS\makensis.exe" set "PATH=%ProgramFiles%\NSIS;%PATH%"
)
where makensis >nul 2>nul || (
  echo [ERROR] NSIS 3.03+ is required to build UniversalDLSS5-Setup-x64.exe.
  echo Install NSIS from https://nsis.sourceforge.io/Download and rerun this file.
  exit /b 1
)

echo ========================================================================
echo UniversalDLSS5 v0.2.8 - GitHub release builder
echo Builds x64 plus x86 bridge/injector, then creates NSIS + portable ZIP.
echo NVIDIA proprietary DLSS/Streamline runtime DLLs are NOT packaged.
echo ========================================================================

call BUILD_WINDOWS.bat %*
if errorlevel 1 exit /b 1

if exist release rmdir /s /q release
mkdir release

python tools\verify_package.py --allow-local-runtime
if errorlevel 1 exit /b 1
python tools\audit_no_readback.py
if errorlevel 1 exit /b 1

echo [PACKAGE] Building NSIS installer...
cpack -G NSIS -C Release -B release -D CPACK_PACKAGE_FILE_NAME=UniversalDLSS5-Setup-x64 --config build\x64\CPackConfig.cmake
if errorlevel 1 exit /b 1

echo [PACKAGE] Building portable ZIP...
cpack -G ZIP -C Release -B release -D CPACK_PACKAGE_FILE_NAME=UniversalDLSS5-Portable-x64 --config build\x64\CPackConfig.cmake
if errorlevel 1 exit /b 1

echo.
echo [OK] GitHub release artifacts:
echo   release\UniversalDLSS5-Setup-x64.exe
echo   release\UniversalDLSS5-Portable-x64.zip
echo.
echo NVIDIA runtime setup is performed after install from the Application page.
endlocal
