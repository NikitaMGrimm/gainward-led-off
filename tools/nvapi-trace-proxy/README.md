# NVAPI trace proxy

Records LED-related NVAPI calls from a local EXPERTool installation. Output is written to `%LOCALAPPDATA%\GainwardLedTrace\NvApiLedTrace.log`.

Run `build.cmd` to build and test the proxy. Run `capture-installed.cmd` to copy EXPERTool into a temporary directory and inject the trace hook. The installed files are not modified.

The launcher is x64-only and assumes system DLL addresses are shared across processes during the current boot. Logging changes call timing. Do not redistribute EXPERTool or `nvapi64.real.dll`.
