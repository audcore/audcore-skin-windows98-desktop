param([string]$VcVars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
Set-Location -LiteralPath $root
$manifest=Get-Content -LiteralPath (Join-Path $root 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$vcRoot=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $VcVars))
$compiler=Join-Path $vcRoot ('Tools/MSVC/'+$manifest.build.toolchain.tools.msvc+'/bin/Hostx64/x64/cl.exe')
if ((Get-Item -LiteralPath $compiler).VersionInfo.FileVersion -ne ($manifest.build.toolchain.compiler.version+'.0')) { throw 'Compiler version mismatch' }
$pythonVersion=& python -c 'import platform; print(platform.python_version())'
if ($pythonVersion -ne $manifest.build.toolchain.tools.python) { throw 'Python version mismatch' }
& powershell.exe -NoProfile -File (Join-Path $root 'skin-contract\build-win98-skin.ps1') -VcVars $VcVars
if ($LASTEXITCODE -ne 0) { throw 'Skin compilation failed' }
$binary=Join-Path $root $manifest.build.intermediate
$info=(Get-Item -LiteralPath $binary).VersionInfo
if ($info.FileVersion -ne $manifest.version -or $info.ProductVersion -ne $manifest.version) { throw 'Manifest and DLL version mismatch' }
Copy-Item -LiteralPath $binary -Destination (Join-Path $root $manifest.build.output)
& python (Join-Path $root 'scripts\audit-release.py') (Join-Path $root $manifest.build.output) --check
if ($LASTEXITCODE -ne 0) { throw 'Native artifact audit failed' }
$dist=New-Item -ItemType Directory -Path (Join-Path $root 'dist') -Force
$target=Join-Path $dist.FullName ($manifest.id+'-v'+$manifest.version+'.skin.audcore')
if (Test-Path -LiteralPath $target) { if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $binary).Hash) { throw 'Cannot overwrite an existing skin version' } } else { Copy-Item -LiteralPath $binary -Destination $target }
Write-Output $target
