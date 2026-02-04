@echo off

:: 鐠佸墽鐤?Visual Studio 2022 閻滎垰顣ㄩ敍宀€鈥樻穱婵呭▏閻?VS2022 閻ㄥ嫬浼愰崗鐑芥懠
CALL "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" amd64
if %errorlevel% NEQ 0 goto error

:: 鐎瑰顥?vcpkg 娓氭繆绂嗛崠?
vcpkg install --triplet=x64-windows-static
if %errorlevel% NEQ 0 goto error

:: 濞撳懐鎮婃稊瀣閻?CMake 缂傛挸鐡ㄩ弬鍥︽
IF EXIST _builds (
    rmdir /S /Q _builds
)
IF EXIST CMakeCache.txt (
    del CMakeCache.txt
)

:: 鐠佸墽鐤?CMake 閺嬪嫬缂撻惄顔肩秿閿涘本瀵氱€规艾浼愰崗鐑芥懠閺傚洣娆㈤崪宀€娲伴弽?triplet閿涘苯浼愰崗鐑芥肠娑?v140
cmake -H. -B_builds ^
    -DEVENT__DISABLE_OPENSSL=ON ^
    -DCMAKE_TOOLCHAIN_FILE=G:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -G "Visual Studio 17 2022" -T v140 -A x64
if %errorlevel% NEQ 0 goto error

:: 閺嬪嫬缂?Release 閻楀牊婀?
cmake --build _builds --config Release
if %errorlevel% NEQ 0 goto error

:: 閺嬪嫬缂?Debug 閻楀牊婀?
cmake --build _builds --config Debug
if %errorlevel% NEQ 0 goto error

:: 鏉╂劘顢戝ù瀣槸
pushd _builds
ctest -C Debug -VV
if %errorlevel% NEQ 0 goto error
popd

goto :EOF

:error
echo Build or test failed. Press any key to exit...
pause
exit /b 1