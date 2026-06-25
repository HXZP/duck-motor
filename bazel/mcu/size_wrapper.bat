@echo off
setlocal
set "SIZE_TOOL=%~1"
set "ELF_FILE=%~2"
set "OUT_FILE=%~3"
"%SIZE_TOOL%" "%ELF_FILE%" > "%OUT_FILE%"
exit /b %ERRORLEVEL%
