# run_app.ps1 - Windows build & launch script for OIM
# Usage: .\run_app.ps1

$ErrorActionPreference = "Stop"

# Ensure Flutter is in PATH
$env:PATH = "C:\flutter\bin;" + $env:PATH

# ---------------------------------------------------------------------------
# Configuration - adjust these paths to match your environment
# ---------------------------------------------------------------------------
$ProjectRoot = $PSScriptRoot
$EmailDir = Join-Path $ProjectRoot "email"
$MlsFfiDir = Join-Path $EmailDir "mls_ffi"
$BuildDir = Join-Path $EmailDir "build"

# vcpkg toolchain file (required for CMake to find dependencies)
$VcpkgToolchain = "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

# Flutter Windows build output
$FlutterBuildDir = Join-Path $ProjectRoot "build\windows\x64\runner\Debug"

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
        -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain `
        -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
}

# Build
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "C++ email_core build failed" }
Write-Host "  email_core built successfully" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Step 3: Build Flutter Windows app
# ---------------------------------------------------------------------------
Write-Host "=== Building Flutter Windows app ===" -ForegroundColor Cyan
Push-Location $ProjectRoot
try {
    flutter build windows --debug
    if ($LASTEXITCODE -ne 0) { throw "Flutter Windows build failed" }
} finally {
    Pop-Location
}
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
$vcpkgBin = "C:\vcpkg\installed\x64-windows\bin"
$depDlls = @(
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll",
    "libcurl.dll",
    "z.dll",
    "iconv-2.dll",
    "sqlite3.dll",
    "gsasl-18.dll",
    "glib-2.0-0.dll",
    "gobject-2.0-0.dll",
    "gio-2.0-0.dll",
    "gmodule-2.0-0.dll",
    "gthread-2.0-0.dll",
    "intl-8.dll",
    "charset-1.dll",
    "pcre2-8.dll",
    "ffi-8.dll",
    "liblzma.dll"
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

# Copy vmime DLL if dynamically linked
$vmimeDll = Join-Path $EmailDir "deps\vmime-install\bin\vmime.dll"
if (Test-Path $vmimeDll) {
    Copy-Item $vmimeDll $FlutterBuildDir -Force
    Write-Host "  Copied vmime.dll"
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
