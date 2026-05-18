$installPath = "$env:USERPROFILE\glang"
$tempPath = "$env:TEMP\glang-build"

Write-Host "Starting glang installation..."

# ---- Ensure install folder exists ----
if (!(Test-Path $installPath)) {
    New-Item -ItemType Directory -Path $installPath | Out-Null
}

# ---- Clean temp build folder ----
if (Test-Path $tempPath) {
    Remove-Item $tempPath -Recurse -Force -ErrorAction SilentlyContinue
}

# ---- Clone repo ----
git clone https://github.com/0scriptus0/glang.git $tempPath

if (!(Test-Path "$tempPath\main.cpp")) {
    Write-Host "Clone failed or missing main.cpp"
    exit 1
}

# ---- Kill running instance ----
taskkill /F /IM glang.exe 2>$null | Out-Null

# ---- Build ----
g++ -std=c++17 -O3 "$tempPath\main.cpp" -o "$tempPath\glang.exe"

if (!(Test-Path "$tempPath\glang.exe")) {
    Write-Host "Build failed"
    exit 1
}

# ---- Install binary (DO NOT TOUCH PACKAGES) ----
Copy-Item "$tempPath\glang.exe" "$installPath\glang.exe" -Force

Write-Host "glang.exe installed"

# ---- Ensure packages folder exists ----
$packages = "$installPath\packages"
if (!(Test-Path $packages)) {
    New-Item -ItemType Directory -Path $packages | Out-Null
    Write-Host "Created packages folder"
}
else {
    Write-Host "Keeping existing packages folder"
}

# ---- PATH FIX (YOUR METHOD, CORRECTED) ----
$glang = "$env:USERPROFILE\glang"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")

if ([string]::IsNullOrEmpty($userPath)) {
    $userPath = ""
}

if ($userPath -notlike "*$glang*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$glang", "User")
    Write-Host "PATH updated"
}
else {
    Write-Host "PATH already contains glang"
}

# ---- TEMP CLEANUP ----
Remove-Item $tempPath -Recurse -Force -ErrorAction SilentlyContinue

# ---- VERIFY INSTALL ----
Write-Host "Verifying installation..."

$cmd = Get-Command glang -ErrorAction SilentlyContinue

if ($cmd) {
    Write-Host "SUCCESS: glang is ready"
}
else {
    Write-Host "Installed but not in active PATH session"
    Write-Host "Fix: close CMD and reopen it"
}

Write-Host "Done."