param(
  [ValidateSet('x64', 'x86')]
  [string[]]$Architecture = @('x64', 'x86')
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
  throw 'Visual Studio Installer and the C++ build tools are required.'
}
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {
  throw 'No Visual Studio installation with the x86/x64 C++ build tools was found.'
}
$environmentScript = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'

foreach ($target in $Architecture) {
  $artifacts = Join-Path $repository "chatgpt_temporaryfolder\tests\TestLanguageBarIcon\$target"
  New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
  $batch = Join-Path $artifacts 'build-and-run.cmd'
  $resource = Join-Path $artifacts 'TestLanguageBarIcon.res'
  $object = Join-Path $artifacts 'TestLanguageBarIcon.obj'
  $executable = Join-Path $artifacts 'TestLanguageBarIcon.exe'
  $zhIcon = Join-Path $repository 'resource\zh.ico'
  $enIcon = Join-Path $repository 'resource\en.ico'
  $commands = @"
@echo off
chcp 65001 >nul
call "$environmentScript" -no_logo -arch=$target -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "$PSScriptRoot"
rc /nologo /fo "$resource" TestLanguageBarIcon.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /W4 /WX /DUNICODE /D_UNICODE /Fo"$object" /Fe"$executable" TestLanguageBarIcon.cpp "$resource" /link /INCREMENTAL:NO user32.lib gdi32.lib
if errorlevel 1 exit /b %errorlevel%
"$executable" "$zhIcon" "$enIcon"
exit /b %errorlevel%
"@
  [IO.File]::WriteAllText($batch, $commands, [Text.UTF8Encoding]::new($false))
  & $env:ComSpec /d /c $batch 2>&1 | Tee-Object -FilePath (Join-Path $artifacts 'test.log')
  if ($LASTEXITCODE -ne 0) {
    throw "$target language bar icon checks failed. See $artifacts\test.log"
  }
}
