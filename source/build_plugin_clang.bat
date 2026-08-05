@echo off
setlocal
set NAME=DockDistributionOffice

clang++ --target=x86_64-pc-windows-msvc -std=c++17 -O2 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-threadsafe-statics -nostdlib -c %NAME%.cpp -o %NAME%.obj
if errorlevel 1 exit /b 1

lld-link /dll /noentry /nodefaultlib /machine:x64 /opt:ref /opt:icf /timestamp:0 /out:%NAME%.dll %NAME%.obj vcruntime140_import.lib /export:TsmPluginApiVersion /export:TsmPluginInit /export:TsmPluginStart
if errorlevel 1 exit /b 1

echo Built %NAME%.dll
endlocal
