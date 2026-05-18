$installPath = "$env:USERPROFILE\glang"

if (Test-Path $installPath) {
    Remove-Item $installPath -Recurse -Force
}

git clone https://github.com/0scriptus0/glang.git $installPath

if (!(Test-Path "$installPath\main.cpp")) {
    Write-Host "Clone failed or missing main.cpp"
    exit 1
}

cd $installPath

taskkill /F /IM glang.exe 2>$null

g++ -std=c++17 -O3 main.cpp -o glang.exe

Write-Host "Build complete: $installPath\glang.exe"

# PATH setup (correct way)
$glangPath = "$env:USERPROFILE\glang"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")

if ($userPath -notlike "*$glangPath*") {
    [Environment]::SetEnvironmentVariable(
        "Path",
        "$userPath;$glangPath",
        "User"
    )
}

Write-Host "Installed and added to PATH"

# cleanup repo files safely (only if they exist)
Remove-Item backend.ps1 -Force -ErrorAction SilentlyContinue
Remove-Item README.md -Force -ErrorAction SilentlyContinue
Remove-Item .gitignore -Force -ErrorAction SilentlyContinue
Remove-Item .gitattributes -Force -ErrorAction SilentlyContinue

# remove src folder safely
if (Test-Path .\src) {
    Remove-Item .\src -Recurse -Force -ErrorAction SilentlyContinue
}

# remove git folder safely (forced unlock attempt)
if (Test-Path .git) {
    attrib -r -s -h .git /s /d
    Remove-Item .git -Recurse -Force -ErrorAction SilentlyContinue
}