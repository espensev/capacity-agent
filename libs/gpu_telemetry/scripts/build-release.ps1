# build-release.ps1 - versioned Release build and packaging for gpu_telemetry
# Usage: .\scripts\build-release.ps1 [-NoVersionBump] [-Version patch|minor|major|x.y.z] [-Sign]

[CmdletBinding()]
param(
    [switch]$KeepBuildDir,
    [ValidateSet('x64')]
    [string]$Architecture = 'x64',
    [string]$Version,
    [switch]$NoVersionBump,
    [switch]$Sign,
    [Alias('h')][switch]$Help
)

if ($Help) {
    Write-Host @"
build-release.ps1 - build and package a versioned gpu_telemetry release

USAGE
    .\scripts\build-release.ps1 [options]

OPTIONS
    -KeepBuildDir       Keep build/ after success
    -Architecture       Build architecture (currently only x64)
    -Version <value>    patch (default), minor, major, or explicit semver
    -NoVersionBump      Use the current VERSION file value unchanged
    -Sign               Sign gpu_telemetry_c.dll with sign-file
    -Help, -h           Show this help text

OUTPUTS
    dist\gpu_telemetry-v<version>-win-<arch>\
        bin\gpu_telemetry_c.dll
        bin\msvcp140.dll
        bin\vcruntime140.dll
        bin\vcruntime140_1.dll
        lib\gpu_telemetry.lib
        lib\gpu_telemetry_c.lib
        include\gpu_telemetry\*.h
        lib\cmake\gpu_telemetry\*.cmake
        share\gpu_telemetry\README.md
        share\gpu_telemetry\VERSION
        build-info.json
        PACKAGE_CONTENTS.json

    dist\gpu_telemetry-v<version>-win-<arch>.zip
    dist\archive\gpu_telemetry-v<version>-win-<arch>.zip
    dist\VERSION_TABLE.json

NOTES
    The VERSION file is the source of truth for release versioning.
    The script builds the C++ library and the C ABI DLL, installs them to a
    staging folder with CMake, bundles the Visual C++ runtime, smoke-tests
    downstream C and C++ consumers against the packaged install tree, then
    turns that validated tree into a shareable dist package.
"@
    return
}

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectName = 'gpu_telemetry'
$PresetName = "$Architecture-release"
$DllName = 'gpu_telemetry_c.dll'
$ImportLibName = 'gpu_telemetry_c.lib'
$StaticLibName = 'gpu_telemetry.lib'
$PublicHeaders = @(
    'gpu_telemetry\gpu_telemetry_c.h',
    'gpu_telemetry\gpu_sensor_reader.h',
    'gpu_telemetry\gpu_probe.h',
    'gpu_telemetry\gpu_snapshot.h'
)

$RepoRoot = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '..\CMakeLists.txt')) {
    (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
} else {
    throw 'Could not locate repository root from scripts/build-release.ps1.'
}

$BuildRoot = Join-Path $RepoRoot 'build'
$BuildDir = Join-Path $BuildRoot $PresetName
$InstallStageDir = Join-Path $BuildRoot 'install-stage'
$PackageValidationDir = Join-Path $BuildRoot 'package-validation'
$DistRoot = Join-Path $RepoRoot 'dist'
$ArchiveDir = Join-Path $DistRoot 'archive'
$VersionFile = Join-Path $RepoRoot 'VERSION'
$ReadmeFile = Join-Path $RepoRoot 'README.md'

function Remove-DirectoryIfExists {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Ensure-Directory {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function New-EmptyDirectory {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    Remove-DirectoryIfExists -Path $Path
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Invoke-External {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$FailureMessage
    )

    $commandText = if ($Arguments.Count -gt 0) {
        "$FilePath $($Arguments -join ' ')"
    } else {
        $FilePath
    }

    Write-Host "  > $commandText" -ForegroundColor DarkGray
    $global:LASTEXITCODE = 0
    try {
        & $FilePath @Arguments
    } catch {
        throw "$FailureMessage ($FilePath): $($_.Exception.Message)"
    }

    $exitCode = $global:LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$FailureMessage (exit code: $exitCode)."
    }
}

function Get-VsWherePath {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    return $null
}

function Get-VsInstallPath {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) {
        return $null
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($installPath) {
        return $installPath.Trim()
    }

    return $null
}

function Get-VsInstanceId {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) {
        return $null
    }

    $instanceId = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property instanceId 2>$null
    if ($instanceId) {
        return $instanceId.Trim()
    }

    return $null
}

function Resolve-VsDevCmdPath {
    $installPath = Get-VsInstallPath
    if ($installPath) {
        $candidate = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $programRoots = @(${env:ProgramFiles(x86)}, $env:ProgramFiles)
    $versions = @('18', '2022', '2019')
    $editions = @('BuildTools', 'Enterprise', 'Professional', 'Community')

    foreach ($root in $programRoots) {
        if (-not $root) { continue }
        foreach ($version in $versions) {
            foreach ($edition in $editions) {
                $candidate = Join-Path $root "Microsoft Visual Studio\$version\$edition\Common7\Tools\VsDevCmd.bat"
                if (Test-Path -LiteralPath $candidate) {
                    return $candidate
                }
            }
        }
    }

    return $null
}

function Resolve-VsInstallPathFromDevCmd {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$VsDevCmdPath)

    $toolsDir = Split-Path -Parent $VsDevCmdPath
    $common7Dir = Split-Path -Parent $toolsDir
    return Split-Path -Parent $common7Dir
}

function Import-VsDevCmdEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$VsDevCmdPath,
        [string]$Arch = 'amd64',
        [string]$HostArch = 'amd64'
    )

    $cmd = "call ""$VsDevCmdPath"" -arch=$Arch -host_arch=$HostArch >nul && set"
    $lines = & cmd.exe /d /s /c $cmd
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize Visual Studio environment via VsDevCmd.bat (exit code: $LASTEXITCODE)."
    }

    foreach ($line in $lines) {
        if (-not $line) { continue }
        $idx = $line.IndexOf('=')
        if ($idx -lt 1) { continue }

        $name = $line.Substring(0, $idx)
        if ($name.StartsWith('=')) { continue }

        $value = $line.Substring($idx + 1)
        [System.Environment]::SetEnvironmentVariable($name, $value, [System.EnvironmentVariableTarget]::Process)
    }
}

function Import-VsDevShellEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$VsInstallPath,
        [string]$VsInstanceId,
        [string]$Arch = 'amd64',
        [string]$HostArch = 'amd64'
    )

    $devShellDll = Join-Path $VsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path -LiteralPath $devShellDll)) {
        throw "Visual Studio DevShell module not found at: $devShellDll"
    }

    Import-Module $devShellDll -ErrorAction Stop

    if ($VsInstanceId) {
        Enter-VsDevShell -VsInstanceId $VsInstanceId -SkipAutomaticLocation -Arch $Arch -HostArch $HostArch -ErrorAction Stop | Out-Null
    } else {
        Enter-VsDevShell -VsInstallPath $VsInstallPath -SkipAutomaticLocation -Arch $Arch -HostArch $HostArch -ErrorAction Stop | Out-Null
    }
}

function Get-ProjectVersion {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$VersionFilePath)

    if (-not (Test-Path -LiteralPath $VersionFilePath)) {
        throw "VERSION file not found at: $VersionFilePath"
    }

    $raw = (Get-Content -LiteralPath $VersionFilePath -Raw).Trim()
    if ($raw -notmatch '^\d+\.\d+\.\d+$') {
        throw "Invalid version format in VERSION file: '$raw'"
    }

    return $raw
}

function Set-ProjectVersion {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$VersionFilePath,
        [Parameter(Mandatory = $true)][string]$NewVersion
    )

    Set-Content -LiteralPath $VersionFilePath -Value $NewVersion -Encoding UTF8 -NoNewline
}

function Step-SemVer {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Current,
        [Parameter(Mandatory = $true)][ValidateSet('patch', 'minor', 'major')][string]$Bump
    )

    $parts = $Current -split '\.'
    $major = [int]$parts[0]
    $minor = [int]$parts[1]
    $patch = [int]$parts[2]

    switch ($Bump) {
        'patch' { $patch++ }
        'minor' { $minor++; $patch = 0 }
        'major' { $major++; $minor = 0; $patch = 0 }
    }

    return "$major.$minor.$patch"
}

function Resolve-VersionParam {
    [CmdletBinding()]
    param(
        [string]$VersionInput,
        [Parameter(Mandatory = $true)][string]$CurrentVersion
    )

    if (-not $VersionInput -or $VersionInput -eq 'patch') {
        return Step-SemVer -Current $CurrentVersion -Bump 'patch'
    }
    if ($VersionInput -eq 'minor') {
        return Step-SemVer -Current $CurrentVersion -Bump 'minor'
    }
    if ($VersionInput -eq 'major') {
        return Step-SemVer -Current $CurrentVersion -Bump 'major'
    }
    if ($VersionInput -notmatch '^\d+\.\d+\.\d+$') {
        throw "Invalid -Version value: '$VersionInput'. Use patch, minor, major, or explicit semver."
    }

    return $VersionInput
}

function Resolve-SignFileCommand {
    $command = Get-Command 'sign-file' -ErrorAction SilentlyContinue
    if ($command) {
        if ($command.Source) { return $command.Source }
        if ($command.Definition) { return $command.Definition }
    }

    throw "Signing requested but the 'sign-file' command was not found."
}

function Resolve-VcRuntimeRedistDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$VsInstallPath,
        [Parameter(Mandatory = $true)][ValidateSet('x64')][string]$ArchitectureValue
    )

    $redistRoot = Join-Path $VsInstallPath 'VC\Redist\MSVC'
    if (-not (Test-Path -LiteralPath $redistRoot)) {
        throw "Visual C++ redistributable root not found at: $redistRoot"
    }

    $versionDirs = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending

    foreach ($versionDir in $versionDirs) {
        $archRoot = Join-Path $versionDir.FullName $ArchitectureValue
        if (-not (Test-Path -LiteralPath $archRoot)) {
            continue
        }

        $crtDir = Get-ChildItem -LiteralPath $archRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } |
            Sort-Object Name -Descending |
            Select-Object -First 1

        if ($crtDir) {
            return $crtDir.FullName
        }
    }

    throw "Visual C++ CRT redistributable directory not found under: $redistRoot"
}

function Invoke-CodeSigning {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$BinaryPath,
        [Parameter(Mandatory = $true)][string]$SignCommandPath
    )

    Invoke-External -FilePath $SignCommandPath -Arguments @($BinaryPath) -FailureMessage 'Code signing failed'

    $signature = Get-AuthenticodeSignature -FilePath $BinaryPath
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        $message = if ($signature.StatusMessage) { $signature.StatusMessage } else { 'Unknown signature validation failure.' }
        throw "Code signing verification failed for ${BinaryPath}: $($signature.Status) - $message"
    }

    return [ordered]@{
        signer = $signature.SignerCertificate.Subject
        thumbprint = $signature.SignerCertificate.Thumbprint
        timeStamper = if ($signature.TimeStamperCertificate) { $signature.TimeStamperCertificate.Subject } else { $null }
    }
}

function Copy-VcRuntimeRedistributables {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$RedistDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationDirectory
    )

    Ensure-Directory -Path $DestinationDirectory

    $copiedDlls = @()
    foreach ($file in Get-ChildItem -LiteralPath $RedistDirectory -Filter '*.dll' -File | Sort-Object Name) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $DestinationDirectory $file.Name) -Force
        $copiedDlls += $file.Name
    }

    if ($copiedDlls.Count -eq 0) {
        throw "No redistributable DLLs were found in: $RedistDirectory"
    }

    return $copiedDlls
}

function Get-Sha256Hex {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-FileVersionMetadata {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$BinaryPath)

    $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($BinaryPath)
    return [ordered]@{
        fileVersion = $info.FileVersion
        productVersion = $info.ProductVersion
        companyName = $info.CompanyName
        fileDescription = $info.FileDescription
        productName = $info.ProductName
        originalFilename = $info.OriginalFilename
    }
}

function Assert-BinaryVersionMetadata {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$BinaryPath,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )

    $metadata = Get-FileVersionMetadata -BinaryPath $BinaryPath

    if ([string]::IsNullOrWhiteSpace($metadata.fileVersion)) {
        throw "Binary is missing FileVersion metadata: $BinaryPath"
    }
    if ([string]::IsNullOrWhiteSpace($metadata.productVersion)) {
        throw "Binary is missing ProductVersion metadata: $BinaryPath"
    }
    if ($metadata.fileVersion -ne $ExpectedVersion) {
        throw "Binary FileVersion '$($metadata.fileVersion)' does not match expected version '$ExpectedVersion'."
    }
    if ($metadata.productVersion -ne $ExpectedVersion) {
        throw "Binary ProductVersion '$($metadata.productVersion)' does not match expected version '$ExpectedVersion'."
    }

    return $metadata
}

function Resolve-BuildArtifactPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$FileName
    )

    $candidates = @(
        (Join-Path $BuildDirectory $FileName),
        (Join-Path $BuildDirectory "Release\$FileName")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "Build artifact not found: $FileName under $BuildDirectory"
}

function Copy-TreeContents {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot
    )

    Ensure-Directory -Path $DestinationRoot
    foreach ($item in Get-ChildItem -LiteralPath $SourceRoot -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $DestinationRoot $item.Name) -Recurse -Force
    }
}

function Clear-DistLatestOutputs {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$DistDirectory,
        [Parameter(Mandatory = $true)][string]$ArchiveDirectory
    )

    Ensure-Directory -Path $DistDirectory
    Ensure-Directory -Path $ArchiveDirectory

    foreach ($item in Get-ChildItem -LiteralPath $DistDirectory -Force) {
        if ($item.FullName -eq $ArchiveDirectory) { continue }
        if ($item.Name -eq 'VERSION_TABLE.json') { continue }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

function New-PackageContentsManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][string]$ManifestPath
    )

    $manifestItems = @()
    foreach ($file in Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File | Sort-Object FullName) {
        if ($file.FullName -eq $ManifestPath) { continue }

        $relative = $file.FullName.Substring($PackageDirectory.Length).TrimStart('\', '/').Replace('\', '/')
        $manifestItems += [ordered]@{
            path = $relative
            size = $file.Length
            sha256 = Get-Sha256Hex -Path $file.FullName
        }
    }

    $manifest = [ordered]@{
        project = $ProjectName
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        files = $manifestItems
    }

    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8
    return $ManifestPath
}

function New-BuildInfo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][string]$PackageName,
        [Parameter(Mandatory = $true)][string]$VersionValue,
        [Parameter(Mandatory = $true)][string]$ArchitectureValue,
        [Parameter(Mandatory = $true)][string]$PresetValue,
        [Parameter(Mandatory = $true)][string]$PackageZipPath,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$DllMetadata,
        [string[]]$BundledRuntimeDlls,
        [System.Collections.IDictionary]$PackageValidation,
        [System.Collections.IDictionary]$SignatureMetadata
    )

    $dllPath = Join-Path $PackageDirectory "bin\$DllName"
    $importLibPath = Join-Path $PackageDirectory "lib\$ImportLibName"
    $staticLibPath = Join-Path $PackageDirectory "lib\$StaticLibName"
    $runtimeEntries = @()

    foreach ($runtimeDll in $BundledRuntimeDlls) {
        $runtimePath = Join-Path $PackageDirectory "bin\$runtimeDll"
        $runtimeEntries += [ordered]@{
            file = $runtimeDll
            path = "bin/$runtimeDll"
            sha256 = Get-Sha256Hex -Path $runtimePath
            size = (Get-Item -LiteralPath $runtimePath).Length
        }
    }

    $buildInfo = [ordered]@{
        project = $ProjectName
        version = $VersionValue
        package = $PackageName
        architecture = $ArchitectureValue
        preset = $PresetValue
        builtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        dll = [ordered]@{
            path = "bin/$DllName"
            sha256 = Get-Sha256Hex -Path $dllPath
            size = (Get-Item -LiteralPath $dllPath).Length
            fileVersion = $DllMetadata.fileVersion
            productVersion = $DllMetadata.productVersion
            fileDescription = $DllMetadata.fileDescription
        }
        importLibrary = [ordered]@{
            path = "lib/$ImportLibName"
            sha256 = Get-Sha256Hex -Path $importLibPath
            size = (Get-Item -LiteralPath $importLibPath).Length
        }
        staticLibrary = [ordered]@{
            path = "lib/$StaticLibName"
            sha256 = Get-Sha256Hex -Path $staticLibPath
            size = (Get-Item -LiteralPath $staticLibPath).Length
        }
        packageZip = [ordered]@{
            file = (Split-Path -Path $PackageZipPath -Leaf)
        }
        publicHeaders = @($PublicHeaders | ForEach-Object { ($_ -replace '\\', '/') })
        runtimeDependencies = $runtimeEntries
        packageValidation = $PackageValidation
        signed = ($null -ne $SignatureMetadata)
    }

    if ($SignatureMetadata) {
        $buildInfo.signature = [ordered]@{
            signer = $SignatureMetadata.signer
            thumbprint = $SignatureMetadata.thumbprint
            timeStamper = $SignatureMetadata.timeStamper
        }
    }

    $buildInfoPath = Join-Path $PackageDirectory 'build-info.json'
    $buildInfo | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $buildInfoPath -Encoding UTF8
    return $buildInfoPath
}

function Invoke-PackageSmokeTests {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$CMakePath,
        [Parameter(Mandatory = $true)][string]$NinjaPath
    )

    New-EmptyDirectory -Path $WorkingDirectory

    $packagePrefix = $PackageDirectory.Replace('\', '/')
    $packageBinDir = Join-Path $PackageDirectory 'bin'

    $cppProjectDir = Join-Path $WorkingDirectory 'cpp'
    $cProjectDir = Join-Path $WorkingDirectory 'c'
    Ensure-Directory -Path $cppProjectDir
    Ensure-Directory -Path $cProjectDir

    @'
cmake_minimum_required(VERSION 3.21)
project(gpu_telemetry_pkg_cpp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(gpu_telemetry CONFIG REQUIRED)
add_executable(gpu_telemetry_pkg_cpp main.cpp)
target_link_libraries(gpu_telemetry_pkg_cpp PRIVATE gpu_telemetry::gpu_telemetry)
'@ | Set-Content -LiteralPath (Join-Path $cppProjectDir 'CMakeLists.txt') -Encoding UTF8

    @'
#include <gpu_telemetry/gpu_sensor_reader.h>

int main() {
    GpuSensorReader reader;
    return reader.is_initialized() ? 0 : 0;
}
'@ | Set-Content -LiteralPath (Join-Path $cppProjectDir 'main.cpp') -Encoding UTF8

    @'
cmake_minimum_required(VERSION 3.21)
project(gpu_telemetry_pkg_c LANGUAGES C)
find_package(gpu_telemetry CONFIG REQUIRED)
add_executable(gpu_telemetry_pkg_c main.c)
target_link_libraries(gpu_telemetry_pkg_c PRIVATE gpu_telemetry::gpu_telemetry_c)
'@ | Set-Content -LiteralPath (Join-Path $cProjectDir 'CMakeLists.txt') -Encoding UTF8

    @'
#include <gpu_telemetry/gpu_telemetry_c.h>

int main(void) {
    gpu_telemetry_reader_t* reader = gpu_telemetry_reader_create();
    gpu_telemetry_reader_destroy(reader);
    return 0;
}
'@ | Set-Content -LiteralPath (Join-Path $cProjectDir 'main.c') -Encoding UTF8

    $cppBuildDir = Join-Path $cppProjectDir 'build'
    $cBuildDir = Join-Path $cProjectDir 'build'

    Invoke-External -FilePath $CMakePath -Arguments @(
        '-S', $cppProjectDir,
        '-B', $cppBuildDir,
        '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$packagePrefix"
    ) -FailureMessage 'Packaged C++ smoke-test configure failed' | Out-Host

    Invoke-External -FilePath $CMakePath -Arguments @(
        '--build', $cppBuildDir, '--parallel'
    ) -FailureMessage 'Packaged C++ smoke-test build failed' | Out-Host

    Invoke-External -FilePath $CMakePath -Arguments @(
        '-S', $cProjectDir,
        '-B', $cBuildDir,
        '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$packagePrefix"
    ) -FailureMessage 'Packaged C smoke-test configure failed' | Out-Host

    Invoke-External -FilePath $CMakePath -Arguments @(
        '--build', $cBuildDir, '--parallel'
    ) -FailureMessage 'Packaged C smoke-test build failed' | Out-Host

    $originalPath = $env:PATH
    try {
        $env:PATH = "$packageBinDir;$originalPath"
        Invoke-External -FilePath (Join-Path $cppBuildDir 'gpu_telemetry_pkg_cpp.exe') -Arguments @() -FailureMessage 'Packaged C++ smoke-test run failed' | Out-Host
        Invoke-External -FilePath (Join-Path $cBuildDir 'gpu_telemetry_pkg_c.exe') -Arguments @() -FailureMessage 'Packaged C smoke-test run failed' | Out-Host
    }
    finally {
        $env:PATH = $originalPath
    }

    return [ordered]@{
        cxx = [ordered]@{
            language = 'C++'
            consumer = 'gpu_telemetry::gpu_telemetry'
            executable = 'gpu_telemetry_pkg_cpp.exe'
            result = 'passed'
        }
        c = [ordered]@{
            language = 'C'
            consumer = 'gpu_telemetry::gpu_telemetry_c'
            executable = 'gpu_telemetry_pkg_c.exe'
            result = 'passed'
        }
    }
}

function New-ZipFromDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$ZipPath
    )

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    $parent = Split-Path -Parent $SourceDirectory

    $zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File | Sort-Object FullName) {
            $relative = $file.FullName.Substring($parent.Length).TrimStart('\', '/').Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip,
                $file.FullName,
                $relative,
                [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    }
    finally {
        $zip.Dispose()
    }
}

function Update-VersionTable {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$VersionTablePath,
        [Parameter(Mandatory = $true)][string]$VersionValue,
        [Parameter(Mandatory = $true)][string]$PackageName,
        [Parameter(Mandatory = $true)][string]$PackageZipPath,
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][string]$ArchitectureValue,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$DllMetadata
    )

    $entries = @()
    if (Test-Path -LiteralPath $VersionTablePath) {
        try {
            $existing = Get-Content -LiteralPath $VersionTablePath -Raw | ConvertFrom-Json
            if ($existing.builds) {
                $entries = @($existing.builds)
            }
        } catch {
            Write-Warning 'Could not parse existing VERSION_TABLE.json, starting fresh.'
        }
    }

    $packageDll = Join-Path $PackageDirectory "bin\$DllName"
    $newEntry = [ordered]@{
        version = $VersionValue
        package = $PackageName
        architecture = $ArchitectureValue
        zip = (Split-Path -Path $PackageZipPath -Leaf)
        zipSha256 = Get-Sha256Hex -Path $PackageZipPath
        dllSha256 = Get-Sha256Hex -Path $packageDll
        fileVersion = $DllMetadata.fileVersion
        builtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    }

    $updatedEntries = @()
    $replaced = $false
    foreach ($entry in $entries) {
        if ($entry.version -eq $VersionValue -and $entry.architecture -eq $ArchitectureValue) {
            $updatedEntries += $newEntry
            $replaced = $true
        } else {
            $updatedEntries += $entry
        }
    }
    if (-not $replaced) {
        $updatedEntries += $newEntry
    }

    $table = [ordered]@{
        project = $ProjectName
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        builds = @($updatedEntries | Sort-Object builtUtc -Descending)
    }

    $table | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $VersionTablePath -Encoding UTF8
    return $VersionTablePath
}

$timer = [System.Diagnostics.Stopwatch]::StartNew()
$buildSucceeded = $false
$versionFileUpdated = $false
$originalVersion = $null
$buildVersion = $null
$resolvedSignCommand = $null
$signatureInfo = $null
$packageName = $null
$packageDir = $null
$packageZip = $null
$archivedZip = $null
$dllMetadata = $null
$vcRuntimeRedistDir = $null
$bundledRuntimeDlls = @()
$packageValidation = $null

Write-Host "--- Build pipeline: $ProjectName (Release) ---" -ForegroundColor Cyan
Write-Host "Architecture : $Architecture"
Write-Host "Preset       : $PresetName"
Write-Host "Project root : $RepoRoot"
Write-Host "Build dir    : $BuildDir"
Write-Host "Install dir  : $InstallStageDir"
Write-Host "Dist dir     : $DistRoot"

try {
    Write-Host "`n[1/10] Version management..." -ForegroundColor Yellow
    $currentVersion = Get-ProjectVersion -VersionFilePath $VersionFile
    $originalVersion = $currentVersion

    if ($NoVersionBump) {
        $buildVersion = $currentVersion
        Write-Host "Version: $buildVersion (no bump)" -ForegroundColor Green
    } else {
        $buildVersion = Resolve-VersionParam -VersionInput $Version -CurrentVersion $currentVersion
        if ($buildVersion -ne $currentVersion) {
            Set-ProjectVersion -VersionFilePath $VersionFile -NewVersion $buildVersion
            $versionFileUpdated = $true
            Write-Host "Version: $currentVersion -> $buildVersion" -ForegroundColor Green
        } else {
            Write-Host "Version: $buildVersion (unchanged)" -ForegroundColor Green
        }
    }

    Write-Host "`n[2/10] Cleaning build staging..." -ForegroundColor Yellow
    Remove-DirectoryIfExists -Path (Join-Path $RepoRoot '.vs')
    Remove-DirectoryIfExists -Path (Join-Path $RepoRoot 'out')
    Remove-DirectoryIfExists -Path $BuildRoot
    New-EmptyDirectory -Path $BuildDir
    New-EmptyDirectory -Path $InstallStageDir
    Clear-DistLatestOutputs -DistDirectory $DistRoot -ArchiveDirectory $ArchiveDir

    Write-Host "`n[3/10] Initializing Visual Studio environment..." -ForegroundColor Yellow
    $vsInstallPath = Get-VsInstallPath
    $vsInstanceId = Get-VsInstanceId
    $devShellLoaded = $false
    if ($vsInstallPath) {
        try {
            Import-VsDevShellEnvironment -VsInstallPath $vsInstallPath -VsInstanceId $vsInstanceId -Arch 'amd64' -HostArch 'amd64'
            $devShellLoaded = $true
            Write-Host "VS environment loaded via DevShell: $vsInstallPath" -ForegroundColor Green
        } catch {
            Write-Verbose "DevShell failed, falling back to VsDevCmd: $($_.Exception.Message)"
        }
    }

    if (-not $devShellLoaded) {
        $vsDevCmd = Resolve-VsDevCmdPath
        if (-not $vsDevCmd) {
            throw 'VsDevCmd.bat not found. Install Visual Studio with C++ build tools.'
        }

        Import-VsDevCmdEnvironment -VsDevCmdPath $vsDevCmd -Arch 'amd64' -HostArch 'amd64'
        if (-not $vsInstallPath) {
            $vsInstallPath = Resolve-VsInstallPathFromDevCmd -VsDevCmdPath $vsDevCmd
        }
        Write-Host "VS environment loaded via VsDevCmd.bat: $vsDevCmd" -ForegroundColor Green
    }

    Write-Host "`n[4/10] Validating prerequisites..." -ForegroundColor Yellow
    $clPath = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
    if (-not $clPath) {
        throw 'MSVC compiler (cl.exe) not found after Visual Studio environment initialization.'
    }

    $rcPath = (Get-Command rc.exe -ErrorAction SilentlyContinue).Source
    if (-not $rcPath) {
        throw 'Windows resource compiler (rc.exe) not found after Visual Studio environment initialization.'
    }

    $cmakeExe = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
    if (-not $cmakeExe) {
        throw 'cmake not found in PATH.'
    }

    $ninjaExe = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
    if (-not $ninjaExe) {
        throw 'ninja not found in PATH.'
    }

    if (-not $vsInstallPath) {
        throw 'Visual Studio installation path could not be resolved for runtime packaging.'
    }

    $vcRuntimeRedistDir = Resolve-VcRuntimeRedistDirectory -VsInstallPath $vsInstallPath -ArchitectureValue $Architecture

    if ($Sign) {
        $resolvedSignCommand = Resolve-SignFileCommand
        Write-Host "signing : enabled" -ForegroundColor Green
        Write-Host "command : $resolvedSignCommand" -ForegroundColor Green
    } else {
        Write-Host "signing : disabled" -ForegroundColor DarkGray
    }

    Write-Host "cl.exe  : $clPath" -ForegroundColor Green
    Write-Host "rc.exe  : $rcPath" -ForegroundColor Green
    Write-Host "cmake   : $cmakeExe" -ForegroundColor Green
    Write-Host "ninja   : $ninjaExe" -ForegroundColor Green
    Write-Host "VC CRT  : $vcRuntimeRedistDir" -ForegroundColor Green

    Write-Host "`n[5/10] CMake configure + build..." -ForegroundColor Yellow
    $env:CMAKE_GENERATOR = 'Ninja'
    Push-Location -LiteralPath $RepoRoot
    try {
        Invoke-External -FilePath $cmakeExe -Arguments @(
            '--preset', $PresetName,
            "-DCMAKE_MAKE_PROGRAM=$ninjaExe"
        ) -FailureMessage 'CMake configure failed'

        Invoke-External -FilePath $cmakeExe -Arguments @(
            '--build', '--preset', $PresetName, '--parallel'
        ) -FailureMessage 'CMake build failed'
    } finally {
        Pop-Location
    }

    $builtDll = Resolve-BuildArtifactPath -BuildDirectory $BuildDir -FileName $DllName

    if ($Sign) {
        Write-Host "`n[6/10] Signing $DllName..." -ForegroundColor Yellow
        $signatureInfo = Invoke-CodeSigning -BinaryPath $builtDll -SignCommandPath $resolvedSignCommand
        Write-Host "Signer     : $($signatureInfo.signer)" -ForegroundColor Green
        Write-Host "Thumbprint : $($signatureInfo.thumbprint)" -ForegroundColor Green
        if ($signatureInfo.timeStamper) {
            Write-Host "Timestamp  : $($signatureInfo.timeStamper)" -ForegroundColor Green
        }
    } else {
        Write-Host "`n[6/10] Skipping code signing." -ForegroundColor DarkGray
    }

    Write-Host "`n[7/10] Installing and assembling dist package..." -ForegroundColor Yellow
    Invoke-External -FilePath $cmakeExe -Arguments @(
        '--install', $BuildDir,
        '--prefix', $InstallStageDir
    ) -FailureMessage 'CMake install failed'

    $packageName = "$ProjectName-v$buildVersion-win-$Architecture"
    $packageDir = Join-Path $DistRoot $packageName
    $packageZip = Join-Path $DistRoot "$packageName.zip"
    $archivedZip = Join-Path $ArchiveDir "$packageName.zip"

    New-EmptyDirectory -Path $packageDir
    Copy-TreeContents -SourceRoot $InstallStageDir -DestinationRoot $packageDir
    Copy-Item -LiteralPath $VersionFile -Destination (Join-Path $packageDir 'VERSION') -Force
    Copy-Item -LiteralPath $ReadmeFile -Destination (Join-Path $packageDir 'README.md') -Force
    $bundledRuntimeDlls = Copy-VcRuntimeRedistributables -RedistDirectory $vcRuntimeRedistDir -DestinationDirectory (Join-Path $packageDir 'bin')

    Write-Host "Package dir : $packageDir" -ForegroundColor Green
    Write-Host "Bundled CRT : $($bundledRuntimeDlls.Count) DLL(s)" -ForegroundColor Green

    Write-Host "`n[8/10] Verifying package contents..." -ForegroundColor Yellow
    $requiredFiles = @(
        "bin\$DllName",
        "lib\$ImportLibName",
        "lib\$StaticLibName",
        'lib\cmake\gpu_telemetry\gpu_telemetryConfig.cmake',
        'lib\cmake\gpu_telemetry\gpu_telemetryTargets.cmake',
        "share\$ProjectName\VERSION",
        "share\$ProjectName\README.md",
        'README.md',
        'VERSION'
    ) + ($PublicHeaders | ForEach-Object { "include\$_" }) + ($bundledRuntimeDlls | ForEach-Object { "bin\$_" })

    foreach ($relativePath in $requiredFiles) {
        $fullPath = Join-Path $packageDir $relativePath
        if (-not (Test-Path -LiteralPath $fullPath)) {
            throw "Required package file missing: $relativePath"
        }
    }

    $packageDll = Join-Path $packageDir "bin\$DllName"
    $dllMetadata = Assert-BinaryVersionMetadata -BinaryPath $packageDll -ExpectedVersion $buildVersion
    Write-Host "DLL version : $($dllMetadata.fileVersion)" -ForegroundColor Green

    if ($Sign) {
        $packageSignature = Get-AuthenticodeSignature -FilePath $packageDll
        if ($packageSignature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Signed package DLL did not verify correctly: $($packageSignature.Status)"
        }
    }

    Write-Host "`n[9/10] Running packaged consumer smoke tests..." -ForegroundColor Yellow
    $packageValidation = Invoke-PackageSmokeTests `
        -PackageDirectory $packageDir `
        -WorkingDirectory $PackageValidationDir `
        -CMakePath $cmakeExe `
        -NinjaPath $ninjaExe

    Write-Host "C++ smoke   : $($packageValidation.cxx.result)" -ForegroundColor Green
    Write-Host "C smoke     : $($packageValidation.c.result)" -ForegroundColor Green

    $buildInfoPath = New-BuildInfo `
        -PackageDirectory $packageDir `
        -PackageName $packageName `
        -VersionValue $buildVersion `
        -ArchitectureValue $Architecture `
        -PresetValue $PresetName `
        -PackageZipPath $packageZip `
        -DllMetadata $dllMetadata `
        -BundledRuntimeDlls $bundledRuntimeDlls `
        -PackageValidation $packageValidation `
        -SignatureMetadata $signatureInfo

    $manifestPath = Join-Path $packageDir 'PACKAGE_CONTENTS.json'
    New-PackageContentsManifest -PackageDirectory $packageDir -ManifestPath $manifestPath | Out-Null

    Write-Host "`n[10/10] Creating zip + version table..." -ForegroundColor Yellow
    New-ZipFromDirectory -SourceDirectory $packageDir -ZipPath $packageZip

    $zipHash = Get-Sha256Hex -Path $packageZip
    $zipSize = (Get-Item -LiteralPath $packageZip).Length
    $buildInfoOnDisk = Join-Path $packageDir 'build-info.json'
    $buildInfoObj = Get-Content -LiteralPath $buildInfoOnDisk -Raw | ConvertFrom-Json
    $buildInfoObj.packageZip = [ordered]@{
        file = (Split-Path -Path $packageZip -Leaf)
        sha256 = $zipHash
        size = $zipSize
    }
    $buildInfoObj | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $buildInfoOnDisk -Encoding UTF8

    Copy-Item -LiteralPath $packageZip -Destination $archivedZip -Force
    $versionTablePath = Update-VersionTable `
        -VersionTablePath (Join-Path $DistRoot 'VERSION_TABLE.json') `
        -VersionValue $buildVersion `
        -PackageName $packageName `
        -PackageZipPath $packageZip `
        -PackageDirectory $packageDir `
        -ArchitectureValue $Architecture `
        -DllMetadata $dllMetadata

    $buildSucceeded = $true
    Write-Host "Zip         : $packageZip" -ForegroundColor Green
    Write-Host "Archive zip : $archivedZip" -ForegroundColor Green
    Write-Host "Version log : $versionTablePath" -ForegroundColor Green
}
catch {
    if ($versionFileUpdated -and $originalVersion) {
        try {
            Set-ProjectVersion -VersionFilePath $VersionFile -NewVersion $originalVersion
            Write-Warning "Build failed; restored VERSION to $originalVersion."
        } catch {
            Write-Warning "Build failed and VERSION could not be restored automatically: $($_.Exception.Message)"
        }
    }

    throw
}
finally {
    Write-Host "`n[cleanup] Finalizing..." -ForegroundColor Yellow
    if ($buildSucceeded) {
        if ($KeepBuildDir) {
            Write-Host "Build directory kept: $BuildRoot"
        } else {
            Remove-DirectoryIfExists -Path $BuildRoot
            Write-Host "Build directory removed: $BuildRoot"
        }
    } else {
        Write-Warning "Build did not complete successfully; keeping build directory for inspection."
    }
}

$timer.Stop()

if (-not $buildSucceeded) {
    return
}

$packageZipHash = Get-Sha256Hex -Path $packageZip
$packageZipSize = (Get-Item -LiteralPath $packageZip).Length

Write-Host "`n--- SUCCESS: $ProjectName v$buildVersion ---" -ForegroundColor Green
Write-Host "Package dir  : $packageDir"
Write-Host "Zip          : $packageZip"
Write-Host "Archive      : $archivedZip"
Write-Host ("Zip size     : {0:N0} bytes" -f $packageZipSize)
Write-Host "Zip SHA256   : $packageZipHash"
Write-Host ("Build time   : {0:mm\:ss\.fff}" -f $timer.Elapsed)
Write-Host "`nPackage contents:" -ForegroundColor Yellow
Get-ChildItem -LiteralPath $packageDir -File | Format-Table Name, @{Label='Size'; Expression = { '{0:N0} bytes' -f $_.Length } } -AutoSize
