param([string]$QtRoot,[string]$MinGWBin,[string]$Executable,[string]$Destination)
$ErrorActionPreference = 'Stop'
$qtBin = Join-Path $QtRoot 'bin'
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item -LiteralPath $Executable -Destination (Join-Path $Destination 'BeverageTwinDemo.exe')
$qmlDestination = Join-Path $Destination 'qml'
New-Item -ItemType Directory -Force -Path $qmlDestination | Out-Null
# Explicit module set for the imports and the selected Fusion control style.
foreach ($module in @('QtQuick.2','QtQuick3D','QtGraphicalEffects','QtQml','QtQuick\Controls.2','QtQuick\Templates.2','QtQuick\Layouts','QtQuick\Window.2')) {
    $source = Join-Path (Join-Path $QtRoot 'qml') $module
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing QML module: $source" }
    $target = Join-Path $qmlDestination $module
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Recurse -Force
}
$platformDir = Join-Path $Destination 'plugins\platforms'
New-Item -ItemType Directory -Force -Path $platformDir | Out-Null
Copy-Item -LiteralPath (Join-Path $QtRoot 'plugins\platforms\qwindows.dll') -Destination $platformDir

function Read-PeImports([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes,0x3c)
    if ([BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550) { throw "Invalid PE file: $Path" }
    $sectionCount = [BitConverter]::ToUInt16($bytes,$pe+6)
    $optionalSize = [BitConverter]::ToUInt16($bytes,$pe+20)
    $optional = $pe+24
    $directoryOffset = if ([BitConverter]::ToUInt16($bytes,$optional) -eq 0x20b) {112} else {96}
    $importsRva = [BitConverter]::ToUInt32($bytes,$optional+$directoryOffset+8)
    if ($importsRva -eq 0) { return }
    $sections = @()
    for ($i=0;$i -lt $sectionCount;$i++) {
        $offset=$optional+$optionalSize+$i*40
        $sections += @{Virtual=[BitConverter]::ToUInt32($bytes,$offset+12);Size=[Math]::Max([BitConverter]::ToUInt32($bytes,$offset+8),[BitConverter]::ToUInt32($bytes,$offset+16));Raw=[BitConverter]::ToUInt32($bytes,$offset+20)}
    }
    $mapRva = {param([uint32]$rva)
        foreach ($section in $sections) {
            if ($rva -ge $section.Virtual -and $rva -lt $section.Virtual+$section.Size) { return [int]($rva-$section.Virtual+$section.Raw) }
        }
        throw "Unmapped PE RVA in $Path"
    }
    $descriptor = & $mapRva $importsRva
    for ($i=0;$i -lt 4096;$i++) {
        $nameRva=[BitConverter]::ToUInt32($bytes,$descriptor+$i*20+12)
        if ($nameRva -eq 0) { break }
        $nameOffset=& $mapRva $nameRva
        $end=$nameOffset
        while ($bytes[$end] -ne 0) { $end++ }
        [Text.Encoding]::ASCII.GetString($bytes,$nameOffset,$end-$nameOffset)
    }
}

$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue((Join-Path $Destination 'BeverageTwinDemo.exe'))
Get-ChildItem -LiteralPath $Destination -Recurse -File -Filter '*.dll' | ForEach-Object {$queue.Enqueue($_.FullName)}
$seen = @{}
$copied = @{}
while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    if ($seen.ContainsKey($file)) {continue}
    $seen[$file]=$true
    foreach ($dependency in @(Read-PeImports $file)) {
        if ($copied.ContainsKey($dependency)) {continue}
        $source = Join-Path $qtBin $dependency
        if (-not (Test-Path -LiteralPath $source)) {$source=Join-Path $MinGWBin $dependency}
        if (Test-Path -LiteralPath $source) {
            $target=Join-Path $Destination $dependency
            Copy-Item -LiteralPath $source -Destination $target -Force
            $copied[$dependency]=$source
            $queue.Enqueue($target)
        } elseif ($dependency -like 'Qt5*.dll' -or $dependency -like 'libgcc*' -or $dependency -like 'libstdc++*' -or $dependency -like 'libwinpthread*') {
            throw "Missing runtime dependency $dependency required by $file"
        }
    }
}
@'
[Paths]
Prefix=.
Plugins=plugins
Qml2Imports=qml
'@ | Set-Content -LiteralPath (Join-Path $Destination 'qt.conf') -Encoding ascii
$copied | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'runtime-manifest.json') -Encoding utf8
Write-Output "Deployed $($copied.Count) dependent libraries and QML/platform plugins."
