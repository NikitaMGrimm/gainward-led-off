@echo off
setlocal
if not defined VSINSTALLDIR (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl.exe >nul 2>&1 || exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 /WX /MT GpuNvapiOff.cpp /Fe:GainwardLedOff.exe /link /SUBSYSTEM:CONSOLE
