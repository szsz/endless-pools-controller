@echo off
setlocal enabledelayedexpansion
set "TOOL=C:\Users\szebe\AppData\Local\Arduino15\packages\esp32\tools\s3-gcc\2021r2-p5\bin\xtensa-esp32s3-elf-addr2line.exe"
set "ELF=C:\Users\szebe\Documents\GitHub\endless-pools-controller\build\esp32.esp32.esp32s3\endless-pools-controller.ino.elf"

rem Usage: decode-backtrace.cmd Backtrace: 0xAAA:0xBBB 0xCCC:0xDDD ...
set "ARGS="
for %%T in (%*) do (
  for /f "tokens=1 delims=:" %%X in ("%%~T") do (
    if /I not "%%~X"=="Backtrace" set "ARGS=!ARGS! %%~X"
  )
)

"%TOOL%" -pfiaC -e "%ELF%" %ARGS%
