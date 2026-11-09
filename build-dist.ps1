# Builds Windows executables with MSYS2 gcc and assembles a playable dist\ folder.
# Requires MSYS2 with gcc (default location C:\Users\<you>\msys64 or C:\msys64).
param([string]$Msys = "")

$root = $PSScriptRoot
if (-not $Msys) {
    foreach ($p in @("$env:USERPROFILE\msys64", "C:\msys64")) { if (Test-Path "$p\usr\bin\gcc.exe") { $Msys = $p; break } }
}
if (-not $Msys) { throw "MSYS2 with gcc not found. Install it (winget install MSYS2.MSYS2, then 'pacman -S gcc make') or pass -Msys <path>." }

$env:MSYSTEM = "MSYS"
$unixRoot = "/" + $root.Substring(0, 1).ToLower() + $root.Substring(2).Replace('\', '/')
& "$Msys\usr\bin\bash.exe" -lc "cd '$unixRoot' && gcc -Wall -Wextra -O2 -o server.exe server.c battleship_logic.c -lpthread && gcc -Wall -Wextra -O2 -o client.exe client.c battleship_logic.c -lpthread"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$dist = Join-Path $root "dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory "$dist\web" | Out-Null
Copy-Item "$root\server.exe", "$root\client.exe" $dist
Copy-Item "$Msys\usr\bin\msys-2.0.dll" $dist
Copy-Item "$root\web\bridge.py", "$root\web\index.html" "$dist\web"

@'
@echo off
rem Starts the game server and the web UI, then opens the browser. Needs Python 3 on PATH.
cd /d "%~dp0"
start "Battleship server" /min server.exe 8080
timeout /t 1 /nobreak >nul
start "" http://127.0.0.1:8000
echo Close this window (or press Ctrl+C) to stop the game.
python web\bridge.py --web-port 8000 --server 127.0.0.1:8080
taskkill /im server.exe /f >nul 2>&1
'@ | Set-Content "$dist\play.bat" -Encoding ascii

@'
@echo off
rem Starts the game server and the terminal client.
cd /d "%~dp0"
start "Battleship server" /min server.exe 8080
timeout /t 1 /nobreak >nul
client.exe 127.0.0.1 8080
taskkill /im server.exe /f >nul 2>&1
'@ | Set-Content "$dist\play-terminal.bat" -Encoding ascii

Write-Host "Done: $dist"
Get-ChildItem $dist -Recurse -File | ForEach-Object { Write-Host "  " $_.FullName.Substring($dist.Length + 1) }
