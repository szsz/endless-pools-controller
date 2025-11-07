@echo off
setlocal EnableExtensions

REM -----------------------------------------------------------------------------
REM OTA build + upload helper for Waveshare ESP32-S3-ETH using Arduino CLI
REM
REM Usage:
REM   scripts\ota-upload.bat [device-ip-or-hostname] [profile]
REM Examples:
REM   scripts\ota-upload.bat
REM   scripts\ota-upload.bat swimmachine.local
REM   scripts\ota-upload.bat 192.168.1.50 esp32s3-eth
REM Notes:
REM   - If no device is specified, default_port from sketch.yaml is used.
REM   - If no profile is specified, default_fqbn from sketch.yaml is used.
REM
REM Prerequisites:
REM   - arduino-cli installed and available in PATH
REM   - ESP32 core installed (e.g. arduino-cli core install esp32:esp32)
REM   - sketch.yaml present in repo root (defines default_fqbn and profiles)
REM   - Project root contains endless-pools-controller.ino
REM Notes:
REM   - Default OTA port is 3232. If your network supports mDNS, hostname is fine.
REM   - If your OTA is password protected, Arduino CLI may prompt for it.
REM -----------------------------------------------------------------------------

REM Accept optional target argument; if omitted, we'll use sketch.yaml default_port
set "TARGET=%~1"

REM Resolve repo root (= parent of this script directory)
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%\.." >nul
set "REPO_ROOT=%CD%"

REM Read defaults from sketch.yaml: default_port and ota_password
set "SKETCH_YAML=%REPO_ROOT%\sketch.yaml"
set "DEFAULT_PORT="
set "OTA_PW="
set "DEFAULT_FQBN="
if not exist "%SKETCH_YAML%" goto :after_sketch_read
for /f "tokens=1,* delims=:" %%A in ('findstr /C:"default_port:" "%SKETCH_YAML%"') do set "DEFAULT_PORT=%%B"
for /f "tokens=1,* delims=:" %%A in ('findstr /C:"ota_password:" "%SKETCH_YAML%"') do set "OTA_PW=%%B"
for /f "tokens=1,* delims=:" %%A in ('findstr /C:"default_fqbn:" "%SKETCH_YAML%"') do set "DEFAULT_FQBN=%%B"
call :trim DEFAULT_PORT
call :trim OTA_PW
call :trim DEFAULT_FQBN
call :escape OTA_PW
:after_sketch_read

REM Fall back to default_port if TARGET not specified
if not defined TARGET (
  if defined DEFAULT_PORT (
    set "TARGET=%DEFAULT_PORT%"
    echo [INFO] Using default_port from sketch.yaml: %TARGET%
  ) else (
    echo [ERROR] No target specified and default_port not set in sketch.yaml.
    echo         Provide a device (IP/hostname) or set default_port in sketch.yaml.
    popd >nul
    exit /b 1
  )
)

REM Prepare optional upload extras (OTA password)
set "UPLOAD_EXTRA="
if defined OTA_PW goto :have_ota_pw
echo [WARN] ota_password not set in sketch.yaml; proceeding without password
goto :after_ota_pw
:have_ota_pw
set "UPLOAD_EXTRA=--upload-field password=%OTA_PW%"
echo [INFO] Using OTA password from sketch.yaml
:after_ota_pw

REM Prepare optional build property to inject OTA_PASSWORD into firmware
set "BUILD_PROPS="
if defined OTA_PW goto :define_build_props
goto :after_build_props
:define_build_props
set "CPP_DEFINE=-DOTA_PASSWORD=\"%OTA_PW%\""
set "BUILD_PROPS=--build-property compiler.cpp.extra_flags=%CPP_DEFINE% --build-property compiler.c.extra_flags=%CPP_DEFINE%"
echo [INFO] Injecting OTA_PASSWORD into build via compiler flags
:after_build_props

REM Check arduino-cli availability
where arduino-cli >nul 2>nul
if errorlevel 1 (
  echo [ERROR] arduino-cli not found in PATH. Install Arduino CLI and retry.
  echo Download: https://arduino.github.io/arduino-cli/latest/installation/
  popd >nul
  exit /b 1
)

REM Determine Arduino CLI profile (optional second argument). Uses sketch.yaml default_fqbn if no profile.
set "PROFILE=%~2"
if defined PROFILE (
  echo [INFO] Using sketch profile: %PROFILE%
) else (
  if defined DEFAULT_FQBN (
    set "FQBN=%DEFAULT_FQBN%,FlashSize=8M,PartitionScheme=custom"
    echo [INFO] Using default_fqbn from sketch.yaml with overrides: %FQBN%
  ) else (
    set "FQBN=esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,FlashSize=8M,PartitionScheme=custom"
    echo [INFO] Using fallback FQBN: %FQBN%
  )
)
set "BUILD_DIR=%REPO_ROOT%\build\arduino"

REM Compile sketch
echo [INFO] Compiling sketch...
if defined PROFILE goto :compile_with_profile
arduino-cli compile -b "%FQBN%" --build-path "%BUILD_DIR%" --export-binaries "%REPO_ROOT%" %BUILD_PROPS%
goto :after_compile
:compile_with_profile
arduino-cli compile --profile "%PROFILE%" --build-path "%BUILD_DIR%" --export-binaries "%REPO_ROOT%" %BUILD_PROPS%
:after_compile
if errorlevel 1 (
  echo [ERROR] Build failed.
  popd >nul
  exit /b 1
)

REM Upload via OTA (Arduino CLI will use espota under the hood)
echo [INFO] Uploading OTA to %TARGET% ...
if defined PROFILE goto :upload_with_profile
arduino-cli upload -p "%TARGET%" -b "%FQBN%" --input-dir "%BUILD_DIR%" %UPLOAD_EXTRA%
goto :after_upload
:upload_with_profile
arduino-cli upload -p "%TARGET%" --profile "%PROFILE%" --input-dir "%BUILD_DIR%" %UPLOAD_EXTRA%
:after_upload
if errorlevel 1 (
  echo [ERROR] OTA upload failed.
  popd >nul
  exit /b 1
)

echo [OK] OTA upload completed successfully.
popd >nul
exit /b 0

REM --- helpers ---
:trim
setlocal EnableDelayedExpansion
set "s=!%1!"
if not defined s ( endlocal & set "%~1=" & goto :eof )
:trim_l
if "!s:~0,1!"==" " set "s=!s:~1!" & goto trim_l
:trim_r
if "!s:~-1!"==" " set "s=!s:~0,-1!" & goto trim_r
endlocal & set "%~1=%s%"
goto :eof

:escape
rem Escape characters in a variable so it is safe to expand inside () blocks.
rem Usage: call :escape VAR_NAME
rem This preserves literal content (including !), and escapes ^ & | < > ( )
setlocal DisableDelayedExpansion
set "v="
call set "v=%%%~1%%"
if not defined v ( endlocal & goto :eof )
rem Escape % by doubling so later %VAR% expansions don't break when value has %
set "v=%v:%=%%%"
set "v=%v:^=^^%"
set "v=%v:&=^&%"
set "v=%v:|=^|%"
set "v=%v:<=^<%"
set "v=%v:>=^>%"
set "v=%v:)=^)%"
set "v=%v:(=^(%"
endlocal & set "%~1=%v%"
goto :eof
