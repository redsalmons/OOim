# run_app.ps1 - Windows build & launch script for OIM
# Usage: .\run_app.ps1

$ErrorActionPreference = "Stop"

# Ensure Flutter is in PATH
$env:PATH = "C:\flutter\bin;" + $env:PATH

# Load MSVC ARM64 environment (E:\vsc is a copied install not registered
# with vswhere, so the toolchain must be activated manually).
$VcvarsAll = "E:\vsc\VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Get-Command link.exe -ErrorAction SilentlyContinue)) {
    cmd /c "`"$VcvarsAll`" arm64 && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2]
        }
    }
}
# CMake needs ninja on PATH for the Ninja generator
$env:PATH = "E:\vsc\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;" + $env:PATH

# ---------------------------------------------------------------------------
# Configuration - adjust these paths to match your environment
# ---------------------------------------------------------------------------
$ProjectRoot = $PSScriptRoot
$EmailDir = Join-Path $ProjectRoot "email"
$MlsFfiDir = Join-Path $EmailDir "mls_ffi"
$BuildDir = Join-Path $EmailDir "build"

# vcpkg toolchain file (required for CMake to find dependencies)
$VcpkgToolchain = "K:\vcpkg\scripts\buildsystems\vcpkg.cmake"
$VcpkgTriplet = "arm64-windows"

# Flutter Windows build output (Ninja: single-config, bundle lands in runner\ directly)
$FlutterBuildDir = Join-Path $ProjectRoot "build\windows\arm64\runner"
$FlutterCmakeBuildDir = Join-Path $ProjectRoot "build\windows\arm64"
$EphemeralDir = Join-Path $ProjectRoot "windows\flutter\ephemeral"
$FlutterEngineArtifacts = "C:\flutter\bin\cache\artifacts\engine\windows-arm64"

# ---------------------------------------------------------------------------
# Step 1: Build Rust MLS FFI
# ---------------------------------------------------------------------------
Write-Host "=== Building Rust MLS FFI ===" -ForegroundColor Cyan
Push-Location $MlsFfiDir
try {
    cargo build --release
    if ($LASTEXITCODE -ne 0) { throw "Rust MLS FFI build failed" }
} finally {
    Pop-Location
}
Write-Host "  Rust MLS FFI built successfully" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Step 2: Build C++ email_core shared library
# ---------------------------------------------------------------------------
Write-Host "=== Building email_core (CMake + MSVC) ===" -ForegroundColor Cyan

# Configure (only needed on first run or when CMakeLists.txt changes)
if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "  Configuring CMake..."
    if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }
    cmake -S $EmailDir -B $BuildDir `
        -G Ninja `
        -DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain `
        -DVCPKG_TARGET_TRIPLET=$VcpkgTriplet `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
}

# Build
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "C++ email_core build failed" }
Write-Host "  email_core built successfully" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Step 3: Build Flutter Windows app
# NOTE: `flutter build windows` hardcodes the "Visual Studio 17 2022" generator,
# which enumerates VS via COM and cannot see the unregistered E:\vsc copy.
# We run the equivalent pipeline manually with Ninja instead.
# ---------------------------------------------------------------------------
Write-Host "=== Building Flutter Windows app ===" -ForegroundColor Cyan

# 3a. Ensure flutter engine artifacts are in windows/flutter/ephemeral
# (flutter_windows.dll.lib etc. are inputs to the wrapper/plugin targets).
$ephemeralInputs = @(
    "flutter_windows.dll", "flutter_windows.dll.lib", "flutter_windows.dll.exp",
    "icudtl.dat",
    "flutter_export.h", "flutter_windows.h", "flutter_messenger.h",
    "flutter_plugin_registrar.h", "flutter_texture_registrar.h"
)
foreach ($f in $ephemeralInputs) {
    $src = Join-Path $FlutterEngineArtifacts $f
    $dst = Join-Path $EphemeralDir $f
    if ((Test-Path $src) -and (-not (Test-Path $dst))) {
        Copy-Item $src $dst -Force
    }
}
if (-not (Test-Path (Join-Path $EphemeralDir "cpp_client_wrapper"))) {
    Copy-Item (Join-Path $FlutterEngineArtifacts "cpp_client_wrapper") $EphemeralDir -Recurse -Force
}

# 3b. Configure + build the windows runner with Ninja
if (-not (Test-Path (Join-Path $FlutterCmakeBuildDir "CMakeCache.txt"))) {
    cmake -S (Join-Path $ProjectRoot "windows") -B $FlutterCmakeBuildDir `
        -G Ninja `
        -DFLUTTER_TARGET_PLATFORM=windows-arm64 `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "Flutter CMake configuration failed" }
}
cmake --build $FlutterCmakeBuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Flutter Windows build failed" }

# 3c. Run flutter_assemble (dart compile + asset bundle via tool_backend.bat)
# then install to assemble the runnable bundle next to oim.exe.
ninja -C $FlutterCmakeBuildDir flutter/flutter_assemble
if ($LASTEXITCODE -ne 0) { throw "flutter_assemble failed" }
cmake --install $FlutterCmakeBuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Flutter bundle install failed" }
Write-Host "  Flutter Windows app built successfully" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Step 4: Copy dependent DLLs to Flutter output directory
# ---------------------------------------------------------------------------
Write-Host "=== Copying dependent DLLs ===" -ForegroundColor Cyan

if (-not (Test-Path $FlutterBuildDir)) {
    throw "Flutter build output directory not found: $FlutterBuildDir"
}

# Copy email_core.dll
$emailCoreDll = Join-Path $BuildDir "Release\email_core.dll"
if (Test-Path $emailCoreDll) {
    Copy-Item $emailCoreDll $FlutterBuildDir -Force
    Write-Host "  Copied email_core.dll"
} else {
    # Try non-Release path (single-config generators)
    $emailCoreDll2 = Join-Path $BuildDir "email_core.dll"
    if (Test-Path $emailCoreDll2) {
        Copy-Item $emailCoreDll2 $FlutterBuildDir -Force
        Write-Host "  Copied email_core.dll"
    } else {
        Write-Host "  WARNING: email_core.dll not found" -ForegroundColor Yellow
    }
}

# Copy vcpkg dependency DLLs
$vcpkgBin = "K:\vcpkg\installed\$VcpkgTriplet\bin"
$depDlls = @(
    "libcrypto-3-arm64.dll",
    "libssl-3-arm64.dll",
    "libcurl.dll",
    "z.dll",
    "iconv-2.dll",
    "charset-1.dll",
    "sqlite3.dll",
    "gsasl-18.dll",
    "libssh2.dll",
    "liblzma.dll",
    "pcre2-8.dll"
)

foreach ($dll in $depDlls) {
    $src = Join-Path $vcpkgBin $dll
    if (Test-Path $src) {
        Copy-Item $src $FlutterBuildDir -Force
        Write-Host "  Copied $dll"
    } else {
        Write-Host "  NOTE: $dll not found at $src (may not be needed)" -ForegroundColor Yellow
    }
}

# Copy vmime DLL if dynamically linked (static build: no-op)
$vmimeDll = "E:\vmime-install\bin\vmime.dll"
if (Test-Path $vmimeDll) {
    Copy-Item $vmimeDll $FlutterBuildDir -Force
    Write-Host "  Copied vmime.dll"
}

# Copy CA bundle for TLS verification (expected next to oim.exe)
$caBundle = "K:\vcpkg\downloads\tools\perl\5.42.2.1\perl\vendor\lib\Mozilla\CA\cacert.pem"
if (Test-Path $caBundle) {
    Copy-Item $caBundle (Join-Path $FlutterBuildDir "ca-bundle.crt") -Force
    Write-Host "  Copied ca-bundle.crt"
} else {
    Write-Host "  WARNING: cacert.pem not found - TLS verification may fail" -ForegroundColor Yellow
}

# gsasl is now from vcpkg, already included in the depDlls list above

Write-Host "  DLL copy complete" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Step 5: Launch app
# ---------------------------------------------------------------------------
Write-Host "=== Launching app ===" -ForegroundColor Cyan
$exePath = Join-Path $FlutterBuildDir "oim.exe"
if (Test-Path $exePath) {
    Start-Process $exePath
    Write-Host "App launched: $exePath" -ForegroundColor Green
} else {
    throw "App executable not found: $exePath"
}
