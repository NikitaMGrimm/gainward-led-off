@echo off
setlocal

set "EXPERTTRACE_SOURCE=%~1"
if not defined EXPERTTRACE_SOURCE set "EXPERTTRACE_SOURCE=%ProgramFiles%\EXPERTool"
if not exist "%EXPERTTRACE_SOURCE%\TBPanel.exe" (
    echo TBPanel.exe was not found in "%EXPERTTRACE_SOURCE%".
    echo Pass the EXPERTool install directory as the first argument.
    exit /b 1
)

tasklist.exe /FI "IMAGENAME eq TBPanel.exe" /NH | find.exe /I "TBPanel.exe" >nul
if not errorlevel 1 (
    echo Close the running EXPERTool instance before starting a capture.
    exit /b 1
)

pushd "%~dp0" || exit /b 1
call build.cmd
set "EXPERTTRACE_BUILD_RESULT=%errorlevel%"
popd
if not "%EXPERTTRACE_BUILD_RESULT%"=="0" exit /b %EXPERTTRACE_BUILD_RESULT%

for /f "usebackq delims=" %%i in (`powershell.exe -NoProfile -Command "$path = Join-Path $env:TEMP ('GainwardLedTrace-' + [guid]::NewGuid().ToString('N')); [void](New-Item -ItemType Directory -Path $path); $path"`) do set "EXPERTTRACE_SANDBOX=%%i"
if not defined EXPERTTRACE_SANDBOX exit /b 1

powershell.exe -NoProfile -Command ^
    "Get-ChildItem -LiteralPath $env:EXPERTTRACE_SOURCE -Force | Copy-Item -Destination $env:EXPERTTRACE_SANDBOX -Recurse -Force;" ^
    "$log = Join-Path $env:LOCALAPPDATA 'GainwardLedTrace\NvApiLedTrace.log';" ^
    "if (Test-Path -LiteralPath $log) { Move-Item -LiteralPath $log -Destination (Join-Path $env:EXPERTTRACE_SANDBOX 'NvApiLedTrace.previous.log') }"
if errorlevel 1 exit /b %errorlevel%

copy /y "%~dp0nvapi64.dll" "%EXPERTTRACE_SANDBOX%\NvapiTraceHook.dll" >nul || exit /b 1
copy /y "%~dp0nvapi64.real.dll" "%EXPERTTRACE_SANDBOX%\nvapi64.real.dll" >nul || exit /b 1

echo Sandbox: %EXPERTTRACE_SANDBOX%
echo Accept the UAC prompt, reproduce the LED action, then close EXPERTool.
powershell.exe -NoProfile -Command "$target = Join-Path $env:EXPERTTRACE_SANDBOX 'TBPanel.exe'; $hook = Join-Path $env:EXPERTTRACE_SANDBOX 'NvapiTraceHook.dll'; $arguments = [char]34 + $target + [char]34 + ' ' + [char]34 + $hook + [char]34; $process = Start-Process -FilePath (Join-Path '%~dp0' 'InjectTraceLauncher.exe') -ArgumentList $arguments -WorkingDirectory $env:EXPERTTRACE_SANDBOX -Verb RunAs -PassThru -Wait; exit $process.ExitCode"
if errorlevel 1 exit /b %errorlevel%

echo Trace: %LOCALAPPDATA%\GainwardLedTrace\NvApiLedTrace.log
