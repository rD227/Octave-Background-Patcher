@echo off
setlocal enabledelayedexpansion

echo =============================================
echo   Octave Background Image Patcher - Build
echo =============================================
echo.

:: Find Octave root - this script is in octave-bg-patcher/ inside home/
:: Go up 2 levels to octave root
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%i in ("%SCRIPT_DIR%\..\..") do set "OCTAVE_ROOT=%%~fi"

echo Octave root: %OCTAVE_ROOT%

set "MINGW64=%OCTAVE_ROOT%\mingw64"
set "QT6=%MINGW64%\qt6"

:: Verify paths
if not exist "%MINGW64%\bin\g++.exe" (
    echo [ERROR] g++.exe not found at %MINGW64%\bin\g++.exe
    exit /b 1
)
if not exist "%QT6%\include\QtCore" (
    echo [ERROR] Qt6 headers not found at %QT6%\include\QtCore
    exit /b 1
)

echo Toolchain: OK
echo.

:: Set up PATH
set "PATH=%MINGW64%\bin;%PATH%"

:: Build flags
set "CXX=%MINGW64%\bin\g++.exe"
set "CXXFLAGS_DLL=-std=c++17 -O2 -Wall -fPIC"
set "CXXFLAGS_EXE=-std=c++17 -O2 -Wall"
set "DEFINES_DLL=-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE"
set "DEFINES_EXE=-DUNICODE -D_UNICODE"

set "INC_QT=%QT6%\include"
set "INC_QTC=%QT6%\include\QtCore"
set "INC_QTG=%QT6%\include\QtGui"
set "INC_QTWG=%QT6%\include\QtWidgets"
set "INC_QSCI=%QT6%\include\Qsci"

set "LIBDIR=%QT6%\lib"

echo [1/2] Building bgpatch.dll ...
"%CXX%" %CXXFLAGS_DLL% %DEFINES_DLL% -I"%INC_QT%" -I"%INC_QTC%" -I"%INC_QTG%" -I"%INC_QTWG%" -I"%INC_QSCI%" -shared -o bgpatch.dll src\bgpatch\main.cpp src\bgpatch\bgpatch.cpp -L"%LIBDIR%" -lQt6Core -lQt6Gui -lQt6Widgets -lqscintilla2_qt6
if errorlevel 1 (
    echo [ERROR] Failed to build bgpatch.dll
    exit /b 1
)
echo   OK - bgpatch.dll

echo [2/2] Building patcher.exe ...
"%CXX%" %CXXFLAGS_EXE% %DEFINES_EXE% -mwindows -municode -o patcher.exe src\patcher\main.cpp -lcomctl32 -lcomdlg32
if errorlevel 1 (
    echo [ERROR] Failed to build patcher.exe
    exit /b 1
)
echo   OK - patcher.exe

echo.
echo =============================================
echo   Build complete!
echo   %SCRIPT_DIR%\bgpatch.dll
echo   %SCRIPT_DIR%\patcher.exe
echo.
echo   Usage: patcher.exe       (configure and launch)
echo          patcher.exe /silent (launch with saved settings)
echo =============================================
