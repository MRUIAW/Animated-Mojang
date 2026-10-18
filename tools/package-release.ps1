param([string]$Version)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$modRoot = Join-Path $projectRoot 'bin/AnimatedMojang'
$tooth = Get-Content -LiteralPath (Join-Path $projectRoot 'tooth.json') -Raw | ConvertFrom-Json
if (-not $Version) { $Version = $tooth.version }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-mc\d+\.\d+)?$' -or $Version -ne $tooth.version) {
    throw 'Package version must match tooth.json.'
}
$manifest = Get-Content -LiteralPath (Join-Path $modRoot 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne $Version -or $manifest.platform -ne 'client' -or $manifest.entry -ne 'AnimatedMojang.dll') {
    throw 'Build manifest does not match the requested client release. Rebuild first.'
}

# An explicit list prevents development notes, PDBs and source assets entering a release.
$files = @(
    'AnimatedMojang.dll', 'manifest.json', 'config/config.json',
    'resources/data/normal_cave.mesh', 'resources/data/normal_cave.particles',
    'resources/textures/animated_mojang/cave/atlas.png',
    'resources/textures/animated_mojang/cave/loading_screen-100.png',
    'resources/sounds/animated_mojang/startup.ogg',
    'resources/textures/animated_mojang/intro/mojang_animation.png',
    'resources/textures/animated_mojang/intro/studios.png',
    'resources/textures/animated_mojang/intro/mojang_card.png',
    'README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'NOTICE.md', 'LICENSE'
)
$files += 0..8 | ForEach-Object { "resources/textures/animated_mojang/title/minecraft_animation/minecraft_$_.png" }
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $modRoot $file) -PathType Leaf)) { throw "Missing runtime file: $file" }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$output = Join-Path $projectRoot "bin/Animated-Mojang-$Version-client-windows-x64.zip"
$temporary = Join-Path $projectRoot ('bin/package-' + [guid]::NewGuid().ToString('N') + '.zip')
try {
    $archive = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, (Join-Path $modRoot $file), "AnimatedMojang/$file", [IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    } finally { $archive.Dispose() }

    $archive = [IO.Compression.ZipFile]::OpenRead($temporary)
    try {
        if ($archive.Entries.Count -ne $files.Count) { throw 'Unexpected archive file count.' }
        foreach ($file in $files) {
            $entry = $archive.GetEntry("AnimatedMojang/$file")
            if (-not $entry) { throw "Missing archive entry: $file" }
            $stream = $entry.Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose(); $hasher.Dispose() }
            $sourceHash = (Get-FileHash -LiteralPath (Join-Path $modRoot $file) -Algorithm SHA256).Hash
            if ($hash -ne $sourceHash) { throw "Archive content mismatch: $file" }
        }
    } finally { $archive.Dispose() }

    Move-Item -LiteralPath $temporary -Destination $output -Force
    Copy-Item -LiteralPath $output -Destination (Join-Path $projectRoot 'bin/Animated-Mojang-client-windows-x64.zip') -Force
    Write-Output "Verified $($files.Count) runtime files: $output"
    Get-FileHash -LiteralPath $output -Algorithm SHA256
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
}
