param([string]$CompilerBin = '', [string]$BuildDir = 'build')
$ErrorActionPreference = 'Stop'
if (!$CompilerBin -and !(Get-Command clang++ -ErrorAction SilentlyContinue)) {
    $localCompiler = Get-ChildItem -Path '.tools/llvm-mingw-*/bin/clang++.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($localCompiler) { $CompilerBin = $localCompiler.DirectoryName }
}
if ($CompilerBin) { $env:PATH = "$CompilerBin;$env:PATH" }
$pythonScripts = python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
if (Test-Path -LiteralPath $pythonScripts) { $env:PATH = "$pythonScripts;$env:PATH" }
cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($CompilerBin) {
    foreach ($runtime in @('libc++.dll', 'libunwind.dll', 'libwinpthread-1.dll', 'libomp.dll')) {
        $runtimePath = Join-Path $CompilerBin $runtime
        if (Test-Path -LiteralPath $runtimePath) { Copy-Item -LiteralPath $runtimePath -Destination $BuildDir -Force }
    }
}
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
