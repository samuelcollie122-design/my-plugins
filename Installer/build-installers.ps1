# Builds SpaceTrack + SpaceMaster and MotionShaper, then packages all three into ONE setup .exe
$ErrorActionPreference = 'Stop'

$installerDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$suiteDir     = Split-Path -Parent $installerDir
$outDir       = Join-Path $suiteDir 'Installers'

function Step($msg) { Write-Host ''; Write-Host "== $msg" -ForegroundColor Cyan }

function Find-CMake {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $vsw = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vsw) {
        $p = & $vsw -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
        if ($p) { return $p }
    }
    return $null
}

function Find-Iscc {
    $paths = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    return $paths | Where-Object { Test-Path $_ } | Select-Object -First 1
}

# Returns ONLY the build folder path (all tool output goes to the console via Out-Host)
function Build-Project($dir) {
    $build = Join-Path $dir 'build'
    & $script:cmake -S $dir -B $build -G 'Visual Studio 18 2026' -A x64 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'Retrying with the default Visual Studio generator...' -ForegroundColor Yellow
        if (Test-Path $build) { Remove-Item $build -Recurse -Force }
        & $script:cmake -S $dir -B $build -A x64 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $dir" }
    }
    & $script:cmake --build $build --config Release | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $dir" }
    return $build
}

function Require-Bundle($build, $name) {
    $bundle = Join-Path $build "${name}_artefacts\Release\VST3\$name.vst3"
    if (-not (Test-Path $bundle)) { throw "Expected build output not found: $bundle" }
}

try {
    Step 'Checking tools'
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'Git was not found. Install Git for Windows (git-scm.com), then open a NEW window and run this again.'
    }
    $script:cmake = Find-CMake
    if (-not $script:cmake) {
        throw 'CMake was not found. In the Visual Studio installer, tick "C++ CMake tools for Windows", then run this again.'
    }
    Write-Host "CMake: $script:cmake"

    $script:iscc = Find-Iscc
    if (-not $script:iscc) {
        Step 'Installing Inno Setup (free installer builder)'
        winget install -e --id JRSoftware.InnoSetup --accept-package-agreements --accept-source-agreements | Out-Host
        $script:iscc = Find-Iscc
        if (-not $script:iscc) { throw 'Inno Setup could not be installed. Download it from jrsoftware.org/isdl.php, install it, then run this again.' }
    }
    Write-Host "Inno Setup: $script:iscc"

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    Step 'Building SpaceTrack + SpaceMaster (first build takes several minutes)'
    $spaceBuild = Build-Project (Join-Path $suiteDir 'SpaceTrack')
    Require-Bundle $spaceBuild 'SpaceTrack'
    Require-Bundle $spaceBuild 'SpaceMaster'

    Step 'Building MotionShaper'
    $motionBuild = Build-Project (Join-Path $suiteDir 'MotionShaper')
    Require-Bundle $motionBuild 'MotionShaper'

    Step 'Packaging the installer'
    & $script:iscc "/DSpaceBuildDir=$spaceBuild" "/DMotionBuildDir=$motionBuild" "/O$outDir" (Join-Path $installerDir 'PluginSuite.iss') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed to build the installer' }

    Step 'Done'
    Get-ChildItem $outDir -Filter *.exe | ForEach-Object { Write-Host ("  " + $_.FullName) -ForegroundColor Green }
    Invoke-Item $outDir
}
catch {
    Write-Host ''
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Copy the red/error lines above and send them to Claude.'
}
