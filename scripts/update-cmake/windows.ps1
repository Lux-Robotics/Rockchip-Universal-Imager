# Update CMake on Windows (one-time or as needed)
# Run in an elevated PowerShell session.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (Get-Command winget -ErrorAction SilentlyContinue) {
    winget upgrade --id Kitware.CMake --source winget --accept-source-agreements --accept-package-agreements
} else {
    Write-Warning "winget not available; downloading latest CMake MSI instead."
    $cmakeMsi = "$Env:TEMP\cmake-windows-x86_64.msi"
    Invoke-WebRequest -Uri "https://github.com/Kitware/CMake/releases/latest/download/cmake-windows-x86_64.msi" -OutFile $cmakeMsi
    Start-Process -FilePath "msiexec.exe" -ArgumentList @(
        "/i", $cmakeMsi,
        "ADD_CMAKE_TO_PATH=System",
        "/qn",
        "/norestart"    
    ) -Wait
}

Write-Host "CMake update complete."
