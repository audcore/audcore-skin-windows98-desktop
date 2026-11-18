param(
    [string]$OutputRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build'),
    [string]$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'cpp\win98'
$webpName = 'libwebp-1.6.0-windows-x64'
$webpCache = Join-Path $root 'build\win98-webp'
$webp = Join-Path $webpCache $webpName
$webpLib = Join-Path $webp 'lib\libwebp.lib'
$versionHeader = Get-Content -LiteralPath (Join-Path $source 'version.h') -Raw
if ($versionHeader -notmatch '(?m)^#define AUDCORE_98_VERSION_TEXT "(\d+\.\d+\.\d+)"') { throw 'Invalid skin version' }
$version = $Matches[1]
$output = Join-Path $OutputRoot "Audcore-98-v$version"
$artifact = Join-Path $output "Audcore-98-v$version.skin.audcore"
$testExe = Join-Path $output 'desktop-test.exe'
$mutex = [Threading.Mutex]::new($false, 'Global\Utae.StreamingBuild.Single')
$locked = $false
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked -or (Get-Process cargo,rustc,cl,link,cmake,clang,clang-cl,cc1plus -ErrorAction SilentlyContinue)) {
        throw 'Another build is running'
    }
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory
    if ($free -lt (2GB / 1KB)) { throw 'Less than 2 GiB of physical memory is available' }
    if (!(Test-Path -LiteralPath $VcVars)) { throw "Missing compiler setup: $VcVars" }

    $webpLibHash = 'F7CCC6F78850640593AD071BB24415B4ADDDE3F70CF611CDA1FE17510B7A65FD'
    if (!(Test-Path -LiteralPath $webpLib) -or (Get-FileHash -LiteralPath $webpLib).Hash -ne $webpLibHash -or
        !(Test-Path -LiteralPath (Join-Path $webp 'include\webp\encode.h'))) {
        New-Item -ItemType Directory -Force -Path $webpCache | Out-Null
        $archive = Join-Path $webpCache 'libwebp.zip'
        $archiveHash = '48886F506B21F62E4661F0F4CBFCA19800897C385128E8902542D29A950C93F1'
        if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive).Hash -ne $archiveHash) {
            & curl.exe -fsSL --max-time 60 "https://storage.googleapis.com/downloads.webmproject.org/releases/webp/$webpName.zip" -o $archive
            if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $archive).Hash -ne $archiveHash) { throw 'WebP library download failed verification' }
        }
        & tar.exe -xf $archive -C $webpCache "$webpName/include" "$webpName/lib/libwebp.lib"
        if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $webpLib).Hash -ne $webpLibHash) { throw 'Invalid static WebP library' }
    }

    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $output = (Resolve-Path -LiteralPath $output).Path
    $artifact = Join-Path $output "Audcore-98-v$version.skin.audcore"
    $testExe = Join-Path $output 'desktop-test.exe'
    $batch = Join-Path $output 'build-win98.cmd'
    $lines = @(
        '@echo off',
        ('call "{0}" >nul || exit /b 1' -f $VcVars)
    )
    $lines += 'set "CL="', 'set "_CL_="', 'set "LINK="', 'set "_LINK_="'
    $lines += 'rc /nologo /i"{1}" /fo"{0}\desktop.res" "{1}\desktop.rc" || exit /b 1' -f $output, $source
    $flags = '/nologo /utf-8 /std:c++17 /EHsc /W4 /O1 /MT /experimental:deterministic "/pathmap:{0}=." /I"{1}\include"' -f $root, $webp
    $libs = 'user32.lib gdi32.lib comctl32.lib uxtheme.lib msimg32.lib dwmapi.lib shell32.lib ole32.lib windowscodecs.lib version.lib "{0}"' -f $webpLib
    $lines += 'cl {0} /LD /Fo"{1}\desktop.obj" "{2}\desktop.cpp" "{1}\desktop.res" /Fe"{3}" /link {4} || exit /b 1' -f $flags,$output,$source,$artifact,$libs
    if ($Test) {
        $lines += 'cl {0} /Fo"{1}\desktop-test.obj" "{2}\desktop_test.cpp" /Fe"{3}" /link {4} || exit /b 1' -f $flags,$output,$source,$testExe,$libs
    }
    [IO.File]::WriteAllLines($batch, $lines, [Text.Encoding]::Default)
    & python (Join-Path $source 'limited_build.py') cmd.exe /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw 'Native skin build failed' }
    $info = (Get-Item -LiteralPath $artifact).VersionInfo
    if ($info.FileVersion -ne $version -or $info.ProductVersion -ne $version) { throw 'Skin resource version mismatch' }
    & python (Join-Path $root 'scripts\audit-release.py') $artifact --check
    if ($LASTEXITCODE -ne 0) { throw 'Native skin binary audit failed' }
    if ($Test) {
        & $testExe $artifact
        if ($LASTEXITCODE -ne 0) { throw 'Native skin tests failed' }
    }
    Write-Output $artifact
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
