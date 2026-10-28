@echo off
rem Builds the public DragonbornsCodex.dll into build-dist (no test bridge, not deployed). Used by tools\package.py
setlocal
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "VCPKG_ROOT=%VSDIR%\VC\vcpkg"
cd /d "%~dp0"
cmake --preset dist || exit /b 1
cmake --build --preset dist || exit /b 1
