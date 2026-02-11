@echo off
setlocal

:: Install minimal deps for CryptoLoader (x86)
if not exist "vcpkg_installed\x86-windows-static\lib\detours.lib" (
    vcpkg install --classic detours --triplet=x86-windows-static --x-install-root=%CD%\vcpkg_installed
    if %errorlevel% NEQ 0 goto error
)

:: Configure (Win32) - build CryptoLoader only to avoid pulling full dependency set
cmake -H. -B_builds_cryptoloader_x86 ^
    -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x86-windows-static ^
    -DVCPKG_MANIFEST_MODE=OFF ^
    -DVCPKG_INSTALLED_DIR=%CD%/vcpkg_installed ^
    -DUPDATESUITE_BUILD_REBORNLAUNCHER=OFF ^
    -DUPDATESUITE_BUILD_UPDATEFORGE=OFF ^
    -G "Visual Studio 17 2022" -A Win32 %*
if %errorlevel% NEQ 0 goto error

cmake --build _builds_cryptoloader_x86 --config Release --target CryptoLoader
if %errorlevel% NEQ 0 goto error

cmake --build _builds_cryptoloader_x86 --config Debug --target CryptoLoader
if %errorlevel% NEQ 0 goto error

echo.
echo CryptoLoader x86 build done. Outputs in bin_x86\(Debug|Release)\CryptoLoader.dll
goto :EOF

:error
pause
