# OpenSpoof — Windows Native Build & Packaging Script (PowerShell 5.1+ Compatible)
# Usage: .\scripts\build.ps1 [-Version "0.2.0"] [-Abis @("arm64-v8a")]
[CmdletBinding()]
param(
    [string]$Version = "0.2.0",
    [string[]]$Abis = @("arm64-v8a")
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $RepoRoot

Write-Host "=== OpenSpoof Local Windows Build (v$Version) ===" -ForegroundColor Cyan

# 1. Locate Android NDK
$NdkPath = $env:ANDROID_NDK_HOME
if (-not $NdkPath -or -not (Test-Path $NdkPath)) {
    $PotentialNdks = Get-ChildItem -Path "$env:LOCALAPPDATA\Android\Sdk\ndk" -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    if ($PotentialNdks) {
        $NdkPath = $PotentialNdks[0].FullName
    }
}
if (-not $NdkPath -or -not (Test-Path $NdkPath)) {
    Write-Error "Android NDK not found. Set ANDROID_NDK_HOME or install it via Android Studio SDK Manager."
}
Write-Host "[OK] Android NDK: $NdkPath" -ForegroundColor Green

# 2. Locate CMake & Ninja
$PotentialCmake = Get-ChildItem -Path "$env:LOCALAPPDATA\Android\Sdk\cmake" -ErrorAction SilentlyContinue | Sort-Object Name -Descending
if ($PotentialCmake) {
    $CmakeDir = Join-Path $PotentialCmake[0].FullName "bin"
    if (Test-Path $CmakeDir) {
        $env:PATH = $CmakeDir + ";" + $env:PATH
    }
}
$CmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
$NinjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $CmakeCmd) {
    Write-Error "CMake not found in PATH or Android SDK."
}
Write-Host "[OK] CMake: $($CmakeCmd.Source)" -ForegroundColor Green
if ($NinjaCmd) {
    Write-Host "[OK] Ninja: $($NinjaCmd.Source)" -ForegroundColor Green
}

$Toolchain = "$NdkPath\toolchains\llvm\prebuilt\windows-x86_64\bin"
if (-not (Test-Path $Toolchain)) {
    Write-Error "NDK toolchain bin directory not found at $Toolchain"
}

# 3. Ensure zygisk.hpp exists
if (-not (Test-Path "src/zygisk.hpp")) {
    Write-Host "[+] Fetching zygisk.hpp..." -ForegroundColor Yellow
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/master/module/jni/zygisk.hpp" -OutFile "src/zygisk.hpp"
}

# 4. Build Dobby static library per-ABI
New-Item -ItemType Directory -Force "module/zygisk" | Out-Null

foreach ($Abi in $Abis) {
    Write-Host "`n>>> Building Dobby static library for $Abi..." -ForegroundColor Cyan
    $DobbyBuild = "third_party/dobby/build/$Abi"
    New-Item -ItemType Directory -Force $DobbyBuild | Out-Null
    
    $GenArg = @()
    if ($NinjaCmd) { $GenArg = @("-G", "Ninja") }
    
    & cmake -B $DobbyBuild $GenArg `
        "-DCMAKE_TOOLCHAIN_FILE=$NdkPath/build/cmake/android.toolchain.cmake" `
        "-DANDROID_ABI=$Abi" `
        "-DANDROID_PLATFORM=android-21" `
        "-DANDROID_STL=c++_static" `
        "-DCMAKE_BUILD_TYPE=Release" `
        "-DDOBBY_GENERATE_SHARED=OFF" `
        "-DDOBBY_BUILD_TEST=OFF" `
        "third_party/dobby"
        
    & cmake --build $DobbyBuild --target dobby_static
    
    $DobbyLib = "$DobbyBuild/libdobby.a"
    if (-not (Test-Path $DobbyLib)) {
        $Found = Get-ChildItem -Path $DobbyBuild -Filter "libdobby.a" -Recurse | Select-Object -First 1
        if ($Found) { Copy-Item $Found.FullName $DobbyLib }
    }
    $DobbySize = (Get-Item $DobbyLib).Length
    Write-Host "[OK] Dobby ready: $DobbyLib ($DobbySize bytes)" -ForegroundColor Green

    # 5. Compile OpenSpoof Zygisk Engine
    Write-Host ">>> Compiling OpenSpoof Zygisk shared library for $Abi..." -ForegroundColor Cyan
    $ClangPrefix = switch ($Abi) {
        "arm64-v8a"   { "aarch64-linux-android21" }
        "armeabi-v7a" { "armv7a-linux-androideabi21" }
        "x86_64"      { "x86_64-linux-android21" }
        "x86"         { "i686-linux-android21" }
    }
    
    $Compiler = "$Toolchain\$ClangPrefix-clang++.cmd"
    $OutSo = "module/zygisk/$Abi.so"
    
    & $Compiler `
        -std=c++17 -shared -fPIC -O2 -ffunction-sections -fdata-sections "-Wl,--gc-sections" `
        -DHAVE_DOBBY -I src -I third_party/dobby/include `
        src/spoof.cpp src/hooks.cpp src/f5_art_hooks.cpp `
        $DobbyLib -llog `
        -o $OutSo
        
    & "$Toolchain\llvm-strip.exe" --strip-unneeded $OutSo
    $SoSize = (Get-Item $OutSo).Length
    Write-Host "[OK] Built $OutSo ($SoSize bytes)" -ForegroundColor Green
}

# 6. Synchronize WebUI & Package flashable module ZIP
Write-Host "`n>>> Synchronizing WebUI and packaging module..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force "module/webroot" | Out-Null
Copy-Item -Force "webroot/index.html" "module/webroot/index.html"

# Update version in module.prop
$PropPath = "module/module.prop"
$PropContent = Get-Content $PropPath -Raw
$PropContent = $PropContent -replace "(?m)^version=.*", "version=v$Version"
Set-Content $PropPath -Value $PropContent -NoNewline

$ZipName = "openspoof-v$Version.zip"
if (Test-Path $ZipName) { Remove-Item -Force $ZipName }

if (Get-Command tar.exe -ErrorAction SilentlyContinue) {
    Push-Location "module"
    & tar.exe -a -cf "..\$ZipName" *
    Pop-Location
} else {
    Compress-Archive -Path "module\*" -DestinationPath $ZipName -Force
}

$ZipInfo = Get-Item $ZipName
$ZipKb = [math]::Round($ZipInfo.Length / 1024, 2)
Write-Host "`n==================================================" -ForegroundColor Green
Write-Host " [SUCCESS] Flashable Module Ready!" -ForegroundColor Green
Write-Host " File: $($ZipInfo.FullName)" -ForegroundColor Yellow
Write-Host " Size: $ZipKb KB" -ForegroundColor Yellow
Write-Host "==================================================" -ForegroundColor Green
