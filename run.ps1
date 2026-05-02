# build-and-run.ps1

cmake --build build --config Debug

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

.\build\Debug\HexPrismWebGPU.exe