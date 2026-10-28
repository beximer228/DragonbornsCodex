@echo off
rem Builds DragonbornsCodex.dll and copies it into the MO2 mod folder (see CHEATMENU_DEPLOY_DIR in CMakeLists.txt)
setlocal
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "VCPKG_ROOT=%VSDIR%\VC\vcpkg"
cd /d "%~dp0"
cmake --preset release || exit /b 1
cmake --build --preset release || exit /b 1
