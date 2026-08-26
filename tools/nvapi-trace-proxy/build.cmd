@echo off
setlocal
if not defined VSINSTALLDIR (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl.exe >nul 2>&1 || exit /b 1
copy /y "%SystemRoot%\System32\nvapi64.dll" nvapi64.real.dll >nul || exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W4 /MT /LD NvapiTraceProxy.cpp nvapi64.def /Fe:nvapi64.dll
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /O2 /EHsc /W4 /MT ProxySmokeTest.cpp /Fe:ProxySmokeTest.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /O2 /EHsc /W4 /MT InjectTraceLauncher.cpp /Fe:InjectTraceLauncher.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /O2 /EHsc /W4 /MT HookSmokeTarget.cpp /Fe:HookSmokeTarget.exe
if errorlevel 1 exit /b %errorlevel%
ProxySmokeTest.exe
if errorlevel 1 exit /b %errorlevel%
copy /y nvapi64.dll NvapiTraceHook.dll >nul || exit /b 1
InjectTraceLauncher.exe "%CD%\HookSmokeTarget.exe" "%CD%\NvapiTraceHook.dll"
