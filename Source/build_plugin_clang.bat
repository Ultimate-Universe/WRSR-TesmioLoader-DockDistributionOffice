@echo off
setlocal

set "NAME=DockDistributionOffice"
set "BUILD_DIR=build"

where clang++ >nul 2>nul
if errorlevel 1 (
    echo ERROR: clang++ was not found on PATH.
    exit /b 1
)

where lld-link >nul 2>nul
if errorlevel 1 (
    echo ERROR: lld-link was not found on PATH.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where llvm-rc >nul 2>nul
if not errorlevel 1 (
    llvm-rc /fo "%BUILD_DIR%\version.res" version.rc
) else (
    where rc >nul 2>nul
    if errorlevel 1 (
        echo ERROR: neither llvm-rc nor rc.exe was found on PATH.
        exit /b 1
    )
    rc /nologo /fo "%BUILD_DIR%\version.res" version.rc
)
if errorlevel 1 exit /b 1

clang++ --target=x86_64-pc-windows-msvc -std=c++17 -O2 ^
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti ^
  -fno-stack-protector -fno-threadsafe-statics -nostdlib ^
  -c "%NAME%.cpp" -o "%BUILD_DIR%\%NAME%.obj"
if errorlevel 1 exit /b 1

lld-link /dll /noentry /nodefaultlib /machine:x64 /release ^
  /dynamicbase /highentropyva /nxcompat /opt:ref /opt:icf /timestamp:0 ^
  /out:"%BUILD_DIR%\%NAME%.dll" ^
  "%BUILD_DIR%\%NAME%.obj" "%BUILD_DIR%\version.res" vcruntime140_import.lib ^
  /export:TsmPluginApiVersion /export:TsmPluginInit /export:TsmPluginStart
if errorlevel 1 exit /b 1

echo Built %BUILD_DIR%\%NAME%.dll
endlocal
