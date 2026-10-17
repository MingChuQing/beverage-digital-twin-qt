param(
    [string]$QtRoot = 'D:\Qt\Qt5.14.0\5.14.0\mingw73_64',
    [string]$MinGWBin = 'D:\Qt\Qt5.14.0\Tools\mingw730_64\bin',
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$qtBin = Join-Path $QtRoot 'bin'
foreach ($required in @((Join-Path $qtBin 'qmake.exe'), (Join-Path $MinGWBin 'mingw32-make.exe'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build tool: $required" }
}
$env:Path = "$qtBin;$MinGWBin;$env:Path"
$buildDir = Join-Path $projectDir 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Push-Location -LiteralPath $buildDir
try {
    & (Join-Path $qtBin 'qmake.exe') '../beverage_twin_demo.pro' 'CONFIG+=release' 'CONFIG-=debug'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & (Join-Path $MinGWBin 'mingw32-make.exe') '-j4'
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    if ($Deploy) {
        $distDir = Join-Path $projectDir 'dist'
        New-Item -ItemType Directory -Force -Path $distDir | Out-Null
        # Build a fresh runtime tree before merging it into the delivery folder.
        # PE dependency discovery avoids this installation's deploy-tool plugin detection issue.
        $stageDir = Join-Path $env:TEMP ('BeverageTwinDemo-' + [guid]::NewGuid().ToString('N'))
        $stageRuntime = Join-Path $stageDir 'runtime'
        New-Item -ItemType Directory -Force -Path $stageRuntime | Out-Null
        try {
            & (Join-Path $projectDir 'tools\deploy_runtime.ps1') -QtRoot $QtRoot -MinGWBin $MinGWBin -Executable (Join-Path $buildDir 'bin\BeverageTwinDemo.exe') -Destination $stageRuntime
            # Runtime JSON can be reloaded without compiling QML/C++.
            Copy-Item -LiteralPath (Join-Path $projectDir 'config') -Destination (Join-Path $stageRuntime 'config') -Recurse -Force
            Get-ChildItem -LiteralPath $stageRuntime | Copy-Item -Destination $distDir -Recurse -Force
        } finally {
            $resolvedStage = [IO.Path]::GetFullPath($stageDir)
            $resolvedTempPrefix = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
            if (-not $resolvedStage.StartsWith($resolvedTempPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected staging cleanup path' }
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
        Copy-Item -LiteralPath (Join-Path $projectDir 'README.md') -Destination (Join-Path $distDir 'README.md')
        $licenseDir = Join-Path $distDir 'licenses'
        New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
        $qtSource = Join-Path (Split-Path $QtRoot -Parent) 'Src\qtbase'
        foreach ($licenseName in @('LICENSE.LGPL3','LICENSE.GPL3','LICENSE.GPL2')) {
            $licenseSource = Join-Path $qtSource $licenseName
            if (Test-Path -LiteralPath $licenseSource) { Copy-Item -LiteralPath $licenseSource -Destination $licenseDir }
        }
        $compilerLicenses = Join-Path (Split-Path $MinGWBin -Parent) 'licenses'
        foreach ($component in @('gcc','mingw-w64','winpthreads')) {
            $licenseSource = Join-Path $compilerLicenses $component
            if (Test-Path -LiteralPath $licenseSource) { Copy-Item -LiteralPath $licenseSource -Destination $licenseDir -Recurse -Force }
        }
    }
} finally { Pop-Location }
Write-Output "Built: $(Join-Path $buildDir 'bin\BeverageTwinDemo.exe')"
