# One-time Windows runner setup for hardware-helper
# Run in an elevated PowerShell session.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Visual Studio 2022 Build Tools + C++ workload
$vswhere = "$Env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstalled = $false
if (Test-Path $vswhere) {
    $vsInstalled = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
}

if (-not $vsInstalled) {
    Write-Host "Installing Visual Studio 2022 Build Tools..."
    $vsBootstrap = "$Env:TEMP\vs_buildtools.exe"
    Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_BuildTools.exe" -OutFile $vsBootstrap
    Start-Process -FilePath $vsBootstrap -ArgumentList @(
        "--quiet",
        "--wait",
        "--norestart",
        "--nocache",
        "--add", "Microsoft.VisualStudio.Workload.VCTools",
        "--includeRecommended",
        "--includeOptional"
    ) -Wait
}

# CMake (adds to PATH)
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "Installing CMake..."
    $cmakeMsi = "$Env:TEMP\cmake-windows-x86_64.msi"
    Invoke-WebRequest -Uri "https://github.com/Kitware/CMake/releases/latest/download/cmake-windows-x86_64.msi" -OutFile $cmakeMsi
    Start-Process -FilePath "msiexec.exe" -ArgumentList @(
        "/i", $cmakeMsi,
        "ADD_CMAKE_TO_PATH=System",
        "/qn",
        "/norestart"
    ) -Wait
}

# MSYS2 + packages
if (-not (Test-Path "C:\msys64\usr\bin\bash.exe")) {
    Write-Host "Installing MSYS2..."
    $msys2Installer = "$Env:TEMP\msys2-x86_64.exe"
    Invoke-WebRequest -Uri "https://github.com/msys2/msys2-installer/releases/latest/download/msys2-x86_64.exe" -OutFile $msys2Installer
    Start-Process -FilePath $msys2Installer -ArgumentList @("--quiet", "--accept-messages", "--accept-licenses") -Wait
}

Write-Host "Updating MSYS2 packages..."
& C:\msys64\usr\bin\bash.exe -lc "pacman -Syu --noconfirm"
& C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm --needed base-devel autoconf automake libtool pkgconf make mingw-w64-x86_64-toolchain mingw-w64-x86_64-libusb"

# libusb-win32 driver files
$libusbDir = "C:\libusb-win32"
if (-not (Test-Path $libusbDir)) {
    Write-Host "Installing libusb-win32 driver files..."
    $zip = "$Env:TEMP\libusb-win32.zip"
    $dir = "$Env:TEMP\libusb-win32"
    Invoke-WebRequest -Uri "https://github.com/mcuee/libusb-win32/releases/download/release_1.4.0.2/libusb-win32-bin-1.4.0.2.zip" -OutFile $zip
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Expand-Archive -Force -Path $zip -DestinationPath $dir
    $dll = Get-ChildItem -Path $dir -Recurse -Filter libusb0.dll | Select-Object -First 1
    if (-not $dll) { throw "libusb0.dll not found in extracted archive" }
    $root = $dll.Directory.Parent.Parent.FullName
    $x86Sys = Join-Path $root "bin\x86\libusb0.sys"
    $x86Dll = Join-Path $root "bin\x86\libusb0_x86.dll"
    $x86Alt = Join-Path $root "bin\x86\libusb0.dll"
    if (-not (Test-Path $x86Dll) -and (Test-Path $x86Alt)) { Copy-Item -Force $x86Alt $x86Dll }
    $x64Sys = Join-Path $root "bin\amd64\libusb0.sys"
    $x64Dll = Join-Path $root "bin\amd64\libusb0.dll"
    if (-not (Test-Path $x86Sys) -or -not (Test-Path $x86Dll) -or -not (Test-Path $x64Sys) -or -not (Test-Path $x64Dll)) {
        throw "libusb-win32 driver files not found under $root"
    }
    if (Test-Path $libusbDir) { Remove-Item -Recurse -Force $libusbDir }
    Copy-Item -Recurse -Force $root $libusbDir
}

Write-Host "Windows runner setup complete. Reopen the shell to pick up PATH changes."
