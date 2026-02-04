@echo off

::call ./install_vcpkg_packages.bat
vcpkg install --triplet=x64-windows-static 
:: build
cmake -H. -B_builds -DEVENT__DISABLE_OPENSSL=ON ^
	-DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
	-DVCPKG_TARGET_TRIPLET=x64-windows-static ^
	-G "Visual Studio 17 2022"  -A x64 %*
if %errorlevel% NEQ 0 goto error

cmake --build _builds --config Release
if %errorlevel% NEQ 0 goto error

cmake --build _builds --config Debug
if %errorlevel% NEQ 0 goto error

:: test
pushd _builds && ctest -C Debug -VV
if %errorlevel% NEQ 0 goto error
popd

goto :EOF

:error
pause