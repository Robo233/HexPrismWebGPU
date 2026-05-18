# build-and-run.ps1

param(
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

function Normalize-Path([string]$Path) {
    [System.IO.Path]::GetFullPath($Path).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
}

$sourceDir = Normalize-Path $PSScriptRoot
$buildDir = Join-Path $sourceDir "build"
$cacheFile = Join-Path $buildDir "CMakeCache.txt"
$cmakeFilesDir = Join-Path $buildDir "CMakeFiles"
$needsConfigure = -not (Test-Path -LiteralPath $cacheFile)

if (-not $needsConfigure) {
    $cacheHome = Select-String `
        -LiteralPath $cacheFile `
        -Pattern "^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$" `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ($null -eq $cacheHome) {
        $needsConfigure = $true
    } else {
        $cachedSourceDir = Normalize-Path $cacheHome.Matches[0].Groups[1].Value

        if ($cachedSourceDir -ine $sourceDir) {
            Write-Host "CMake cache points to '$cachedSourceDir'; regenerating for '$sourceDir'."
            Remove-Item -LiteralPath $cacheFile -Force

            if (Test-Path -LiteralPath $cmakeFilesDir) {
                Remove-Item -LiteralPath $cmakeFilesDir -Recurse -Force
            }

            $needsConfigure = $true
        }
    }
}

Push-Location -LiteralPath $sourceDir
try {
    if ($needsConfigure) {
        cmake --preset windows-debug

        if ($LASTEXITCODE -ne 0) {
            Write-Error "Configure failed with exit code $LASTEXITCODE"
            exit $LASTEXITCODE
        }
    }

    cmake --build $buildDir --config Debug

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }

    if (-not $BuildOnly) {
        & (Join-Path $buildDir "Debug\HexPrismWebGPU.exe")
    }
} finally {
    Pop-Location
}
