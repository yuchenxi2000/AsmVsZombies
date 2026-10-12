$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $PSScriptRoot
$version = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'version.json') | ConvertFrom-Json
if ($version.version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid loader release version' }
$packageName = "AvZLoader-$($version.version)"
$outputDirectory = Join-Path $packageRoot 'dist/avzloader'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$archivePath = Join-Path $outputDirectory "$packageName.zip"
$checksumPath = Join-Path $outputDirectory "$packageName.sha256"

# A fresh snapshot avoids stale binaries and never modifies the original bin/.
$snapshot = Join-Path $packageRoot ("build/loader-release/" + $packageName + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $snapshot | Out-Null
function Copy-PackageFile([string] $relativePath) {
    $sourcePath = Join-Path $packageRoot $relativePath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Missing release file: $relativePath" }
    $destinationPath = Join-Path $snapshot $relativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
}
function Copy-PackageTree([string] $relativeDirectory) {
    $sourceDirectory = Join-Path $packageRoot $relativeDirectory
    foreach ($file in Get-ChildItem -LiteralPath $sourceDirectory -File -Recurse) {
        $relativePath = $file.FullName.Substring($packageRoot.Length + 1)
        # Exclude VCS, draft and generated build data from source trees.
        if ($relativePath -match '(^|[\\/])(\.git|\.cache|CMakeFiles|draft|build)([\\/]|$)') { continue }
        if ($file.Name -in @('CMakeCache.txt', 'cmake_install.cmake', 'Makefile', 'crash.txt')) { continue }
        if ($file.Extension -in @('.o', '.obj', '.dll', '.exe', '.a', '.zip', '.log')) { continue }
        Copy-PackageFile $relativePath
    }
}
function Get-PackageHash([string] $path) {
    $stream = [System.IO.File]::OpenRead($path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

foreach ($binary in @('avzloader.dll', 'instloader.exe', 'injector.exe', 'libavz.a',
        'lazy.dll', 'autogarden.dll', 'pe12.dll', 'advpause.dll')) {
    Copy-PackageFile "bin/$binary"
}
foreach ($file in @('.gitignore', 'README.md', 'CHANGELOG_LOADER.md', 'CHANGELOG.md', 'LICENSE',
        'MigrationGuide.md', 'CMakeLists.txt', 'metadata.json',
        'loader/avzloader.cpp', 'loader/instloader.cpp', 'loader/mod_manager.h',
        'loader/directory_monitor.h', 'loader/version.json', 'loader/package_release.ps1',
        'loader/tomlplusplus/LICENSE', 'tools/injector/CMakeLists.txt')) {
    Copy-PackageFile $file
}
foreach ($directory in @('inc', 'src', 'mod', 'tutorial', 'tests',
        'loader/tomlplusplus/include', 'tools/injector/src')) {
    Copy-PackageTree $directory
}

# Hash every bundled file so the archive can be checked after extraction.
$manifest = foreach ($file in Get-ChildItem -LiteralPath $snapshot -File -Recurse | Sort-Object FullName) {
    $relativePath = $file.FullName.Substring($snapshot.Length + 1).Replace('\', '/')
    $hash = Get-PackageHash $file.FullName
    "$hash  $relativePath"
}
[System.IO.File]::WriteAllLines((Join-Path $snapshot 'SHA256SUMS.txt'), [string[]] $manifest,
    (New-Object System.Text.UTF8Encoding $false))
# Use .NET directly so packaging does not depend on optional PowerShell modules.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporaryArchive = "$snapshot.zip"
[System.IO.Compression.ZipFile]::CreateFromDirectory($snapshot, $temporaryArchive,
    [System.IO.Compression.CompressionLevel]::Optimal, $false)
Move-Item -LiteralPath $temporaryArchive -Destination $archivePath -Force
$archiveHash = Get-PackageHash $archivePath
[System.IO.File]::WriteAllText($checksumPath, "$archiveHash  $packageName.zip`r`n", [System.Text.Encoding]::ASCII)
Write-Output "Created $archivePath"
Write-Output "SHA256 $archiveHash"
Write-Output "Snapshot $snapshot"
