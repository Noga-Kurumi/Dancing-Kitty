param(
    [string]$OnnxRuntimeRoot = '',
    [string]$CRuntimeRoot = '',
    [string]$CMakeExecutable = 'cmake',
    [string]$PythonExecutable = 'python'
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if (!$OnnxRuntimeRoot) {
    $dependencyRoot = Join-Path $projectRoot '.deps'
    New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
    $archive = Join-Path $dependencyRoot 'onnxruntime-1.23.2.zip'
    if (!(Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.23.2/microsoft.ml.onnxruntime.1.23.2.nupkg' -OutFile $archive
    }
    $expectedHash = '25FE172F7FCDF34F2B5B02C8B997F6B88ABC282956849D4AF4335AF23D0C4E4A'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'ONNX Runtime package checksum mismatch.' }
    $OnnxRuntimeRoot = Join-Path $dependencyRoot 'onnxruntime'
    if (!(Test-Path -LiteralPath (Join-Path $OnnxRuntimeRoot 'build/native/include/onnxruntime_cxx_api.h'))) {
        Expand-Archive -LiteralPath $archive -DestinationPath $OnnxRuntimeRoot -Force
    }
}
if (!(Test-Path -LiteralPath (Join-Path $OnnxRuntimeRoot 'build/native/include/onnxruntime_cxx_api.h'))) { throw 'OnnxRuntimeRoot must point to the extracted native development package.' }
if (!$CRuntimeRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools or supply CRuntimeRoot.' }
    $visualStudio = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$visualStudio) { throw 'Visual Studio 2022 with C++ tools was not found.' }
    $redist = Get-ChildItem -Path (Join-Path $visualStudio 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
    if (!$redist) { throw 'C++ x64 redistributable files were not found. Supply CRuntimeRoot.' }
    $CRuntimeRoot = $redist.FullName
}
& $CMakeExecutable -S $projectRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64 "-DORT_ROOT=$OnnxRuntimeRoot" '-DBUILD_TESTING=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $CMakeExecutable --build $buildRoot --config Release -- /m:1 /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
$ctest = Join-Path (Split-Path -Parent (Get-Command $CMakeExecutable).Source) 'ctest.exe'
& $ctest --test-dir $buildRoot -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed.' }
& $PythonExecutable (Join-Path $projectRoot 'scripts/package.py') --build-root $buildRoot --ort-root $OnnxRuntimeRoot --crt-root $CRuntimeRoot
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
$launcherBuild = Join-Path $buildRoot 'launcher'
$generated = Join-Path $buildRoot 'bundle-generated'
& $CMakeExecutable -S (Join-Path $projectRoot 'src/launcher') -B $launcherBuild -G 'Visual Studio 17 2022' -A x64 "-DBUNDLE_GENERATED_DIR=$generated"
if ($LASTEXITCODE -ne 0) { throw 'Launcher configuration failed.' }
& $CMakeExecutable --build $launcherBuild --config Release -- /m:1 /nr:false
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed.' }
& $PythonExecutable (Join-Path $projectRoot 'scripts/package.py') --build-root $buildRoot --finalize
if ($LASTEXITCODE -ne 0) { throw 'Release asset generation failed.' }
Write-Output "Dancing Kitty $version is ready in $(Join-Path $projectRoot 'dist')"
