@echo off
rem Builds a Release version of Smart Home Planner for Windows and makes a .zip that others can unpack and run.
rem Run it from the "Qt 6.11.2 (MSVC 2022 64-bit)" command prompt (Start menu), or set QT_DIR first:
rem     set QT_DIR=C:\Qt\6.11.2\msvc2022_64
rem     tools\package_windows.bat
setlocal
set SRC=%~dp0..
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.11.2\msvc2022_64
set BUILD=%SRC%\build\release-windows
set STAGE=%BUILD%\install

cmake -S "%SRC%" -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" -DSHP_DEV_LIBRARY_PATH=OFF -DSHP_BUILD_TESTS=OFF || exit /b 1
cmake --build "%BUILD%" --config Release --parallel || exit /b 1
if exist "%STAGE%" rmdir /s /q "%STAGE%"
rem install = exe + Libraries + Qt DLLs / plugins (windeployqt)
cmake --install "%BUILD%" --config Release --prefix "%STAGE%" || exit /b 1
if not exist "%SRC%\dist" mkdir "%SRC%\dist"
powershell -NoProfile -Command "Compress-Archive -Force -Path '%STAGE%\*' -DestinationPath '%SRC%\dist\SmartHomePlanner-windows.zip'"
echo.
echo Done: %SRC%\dist\SmartHomePlanner-windows.zip
