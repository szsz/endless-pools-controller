[CmdletBinding()]
param(
  [Parameter(Position=0)]
  [string]$Target,
  [Parameter(Position=1)]
  [string]$Profile
)

$ErrorActionPreference = 'Stop'

# Resolve repo root (= parent of this script directory)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir
$sketchYaml = Join-Path $repoRoot 'sketch.yaml'
$buildDir   = Join-Path $repoRoot 'build\arduino'

function Clean-YamlValue([string]$s) {
  if ($null -eq $s) { return $null }
  $s = $s.Trim()
  # Strip inline comments (naive, good enough for simple key: value lines)
  if ($s -match '^(.*?)(\s+#.*)?$') { $s = $matches[1].Trim() }
  if ($s.StartsWith('"') -and $s.EndsWith('"') -and $s.Length -ge 2) {
    $s = $s.Substring(1, $s.Length - 2)
  } elseif ($s.StartsWith("'") -and $s.EndsWith("'") -and $s.Length -ge 2) {
    $s = $s.Substring(1, $s.Length - 2)
  }
  return $s
}

function Get-EspotaPath() {
  $base = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\hardware\esp32'
  if (Test-Path -LiteralPath $base) {
    $versions = Get-ChildItem -Directory -LiteralPath $base | Sort-Object Name -Descending
    foreach ($v in $versions) {
      $p1 = Join-Path $v.FullName 'tools\espota.py'
      if (Test-Path -LiteralPath $p1) { return $p1 }
      $p2 = Join-Path $v.FullName 'tools\espota\espota.py'
      if (Test-Path -LiteralPath $p2) { return $p2 }
    }
  }
  return $null
}

function Resolve-Python() {
  # Prefer the Python Launcher with -3 (Python 3)
  $py = Get-Command py -ErrorAction SilentlyContinue
  if ($py) {
    $v = & $py.Source -3 --version 2>$null
    if ($LASTEXITCODE -eq 0 -and ($v -match 'Python\s+3\.\d+')) {
      return @{ Exe = $py.Source; Args = @('-3') }
    }
  }

  # Then try python and python3, avoiding WindowsApps alias stub
  foreach ($name in @('python','python3')) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd -and ($cmd.Path -notlike "*\WindowsApps\*.exe")) {
      $v = & $cmd.Source --version 2>$null
      if ($LASTEXITCODE -eq 0 -and ($v -match 'Python\s+3\.\d+')) {
        return @{ Exe = $cmd.Source; Args = @() }
      }
    }
  }

  # Fallback: look in common install locations
  $dirs = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Python'),
    (Join-Path $env:ProgramFiles 'Python')
  ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

  foreach ($d in $dirs) {
    Get-ChildItem -Directory -LiteralPath $d -ErrorAction SilentlyContinue | ForEach-Object {
      $p = Join-Path $_.FullName 'python.exe'
      if (Test-Path -LiteralPath $p) {
        $v = & $p --version 2>$null
        if ($LASTEXITCODE -eq 0 -and ($v -match 'Python\s+3\.\d+')) {
          return @{ Exe = $p; Args = @() }
        }
      }
    }
  }

  return $null
}

# Read defaults from sketch.yaml: default_port and ota_password
$defaultPort = $null
$otaPw = $null
$defaultFqbn = $null
if (Test-Path -LiteralPath $sketchYaml) {
  $yaml = Get-Content -Raw -LiteralPath $sketchYaml
  if ($yaml -match '(?m)^\s*default_port:\s*(.+)$') {
    $defaultPort = Clean-YamlValue $matches[1]
  }
  if ($yaml -match '(?m)^\s*ota_password:\s*(.+)$') {
    $otaPw = Clean-YamlValue $matches[1]
  }
  if ($yaml -match '(?m)^\s*default_fqbn:\s*(.+)$') {
    $defaultFqbn = Clean-YamlValue $matches[1]
  }
}
# Treat placeholder as unset so otapassword.h default is used
if ($otaPw -and $otaPw -eq 'REPLACE_WITH_OTA_PASSWORD') { $otaPw = $null }

# Fall back to default_port if TARGET not specified
if (-not $Target) {
  if ($defaultPort) {
    $Target = $defaultPort
    Write-Host "[INFO] Using default_port from sketch.yaml: $Target"
  } else {
    Write-Error "No target specified and default_port not set in sketch.yaml. Provide a device (IP/hostname) or set default_port in sketch.yaml."
    exit 1
  }
}

# Check arduino-cli availability
$arduino = Get-Command arduino-cli -ErrorAction SilentlyContinue
if (-not $arduino) {
  Write-Error "arduino-cli not found in PATH. Install Arduino CLI and retry.`nDownload: https://arduino.github.io/arduino-cli/latest/installation/"
  exit 1
}

# Prepare optional build property to inject OTA_PASSWORD into firmware
$buildProps = @()
if ($otaPw) {
  # Escape for C string literal first (backslashes and quotes)
  $pwC = $otaPw -replace '\\','\\\\' -replace '"','\"'
  # Wrap with escaped quotes for the compiler define (GCC-style): -DOTA_PASSWORD=\"...\"
  $cppDefine = "-DOTA_PASSWORD=\`"$pwC\`""
  $buildProps += @('--build-property', "compiler.cpp.extra_flags=$cppDefine",
                   '--build-property', "compiler.c.extra_flags=$cppDefine")
  Write-Host "[INFO] Injecting OTA_PASSWORD into build via compiler flags"
} else {
  Write-Host "[WARN] ota_password not set in sketch.yaml; proceeding without password (using otapassword.h default)"
}

# Determine profile info
if ($Profile) {
  Write-Host "[INFO] Using sketch profile: $Profile"
} else {
  if ($defaultFqbn) {
    $fqbn = "$defaultFqbn,FlashSize=8M,PartitionScheme=custom"
    Write-Host "[INFO] Using default_fqbn with overrides: $fqbn"
  } else {
    $fqbn = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,FlashSize=8M,PartitionScheme=custom"
    Write-Host "[INFO] Using fallback FQBN: $fqbn"
  }
}

# Compile sketch (Arduino CLI will read sketch.yaml automatically)
Write-Host "[INFO] Compiling sketch..."
$compileArgs = @('compile', '-b', $fqbn, '--build-path', $buildDir, '--export-binaries', $repoRoot) + $buildProps
if ($Profile) {
  $compileArgs = @('compile', '--profile', $Profile, '--build-path', $buildDir, '--export-binaries', $repoRoot) + $buildProps
}
& $arduino.Source $compileArgs
if ($LASTEXITCODE -ne 0) {
  Write-Error "[ERROR] Build failed."
  exit 1
}

# Upload (decide network vs serial based on target format)
$serialPortPattern = '^(?i)COM\d+$'
if ($Target -notmatch $serialPortPattern) {
  Write-Host "[INFO] Uploading via espota to $($Target):3232 ..."
  $espota = Get-EspotaPath
  if (-not $espota) {
    Write-Error "espota.py not found in ESP32 core tools. Ensure ESP32 core is installed."
    exit 1
  }
  # Locate built app binary (*.ino.bin)
  $appBin = Get-ChildItem -LiteralPath $buildDir -Filter '*.bin' | Where-Object { $_.Name -match '\.ino\.bin$' } | Select-Object -First 1
  if (-not $appBin) {
    Write-Error "Built app binary (*.ino.bin) not found in $buildDir"
    exit 1
  }
  $args = @('-i', $Target, '-p', '3232', '-f', $appBin.FullName)
  if ($otaPw) { $args += @('-a', $otaPw) }
  $pyCmd = Resolve-Python
  if (-not $pyCmd) {
    Write-Error "Python 3 interpreter not found or not yet available in this shell. Close and reopen your terminal so PATH updates take effect, or ensure Python 3 is on PATH. You can also run espota manually with your Python path."
    exit 1
  }
  $cmdArgs = @() + $pyCmd.Args + @($espota) + $args
  & $pyCmd.Exe @cmdArgs
  if ($LASTEXITCODE -ne 0) {
    Write-Error "[ERROR] OTA upload failed."
    exit 1
  }
} else {
  Write-Host "[INFO] Uploading via serial to $Target ..."
  $uploadArgs = @('upload', '-p', $Target, '-b', $fqbn, '--input-dir', $buildDir)
  if ($Profile) {
    $uploadArgs = @('upload', '-p', $Target, '--profile', $Profile, '--input-dir', $buildDir)
  }
  & $arduino.Source $uploadArgs
  if ($LASTEXITCODE -ne 0) {
    Write-Error "[ERROR] Serial upload failed."
    exit 1
  }
}

Write-Host "[OK] Upload completed successfully."
exit 0
