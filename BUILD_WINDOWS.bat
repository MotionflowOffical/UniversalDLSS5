@echo off
setlocal EnableExtensions
cd /d "%~dp0"
where cmake >nul 2>nul || (echo [ERROR] CMake is required. & exit /b 1)
where git >nul 2>nul || (echo [ERROR] Git is required for pinned MinHook/NVIDIA NGX dependencies. & exit /b 1)

echo ========================================================================
echo UniversalDLSS5 v0.3.2 - Release build x64 + x86 bridge/injector
echo Direct in-game NR + external-host fallback; optional Streamline feature 1004 when plugin files are supplied
echo ========================================================================
cmake -S . -B build\x64 -A x64 -DUDLSS_BUILD_TESTS=ON -DUDLSS_WITH_NGX_NR=ON -DUDLSS_WITH_STREAMLINE=ON %*
if errorlevel 1 exit /b 1
cmake --build build\x64 --config Release --parallel
if errorlevel 1 exit /b 1
ctest --test-dir build\x64 -C Release --output-on-failure
if errorlevel 1 exit /b 1

cmake -S . -B build\x86 -A Win32 -DUDLSS_BUILD_TESTS=OFF -DUDLSS_WITH_NGX_NR=OFF -DUDLSS_WITH_STREAMLINE=OFF %*
if errorlevel 1 exit /b 1
cmake --build build\x86 --config Release --target UniversalDLSS5.Injector UniversalDLSS5.Bridge --parallel
if errorlevel 1 exit /b 1

set OUT=build\x64\bin\Release
if not exist "%OUT%" set OUT=build\x64\bin
set OUT32=build\x86\bin\Release
if not exist "%OUT32%" set OUT32=build\x86\bin
copy /y "%OUT32%\UniversalDLSS5.Injector.exe" "%OUT%\UniversalDLSS5.Injector32.exe" >nul
copy /y "%OUT32%\UniversalDLSS5.Bridge.dll" "%OUT%\UniversalDLSS5.Bridge32.dll" >nul
python tools\audit_no_readback.py
if errorlevel 1 exit /b 1
python tools\verify_package.py --allow-local-runtime
if errorlevel 1 exit /b 1

echo.
echo [OK] Build output: %OUT%
echo External NR host: %OUT%\UniversalDLSS5.NRHost.exe
echo NVIDIA runtime: use Application ^> Import NVIDIA SDK... or place nvngx_dlssnr.dll in the selected runtime folder.
echo Optional Streamline 1004 stack: sl.interposer.dll + sl.common.dll + sl.dlss_nr.dll
echo Start UniversalDLSS5.exe as the same user as the target application.
endlocal
