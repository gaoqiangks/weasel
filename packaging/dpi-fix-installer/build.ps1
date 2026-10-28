[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$MakeNsis,
  [Parameter(Mandatory = $true)][string]$OutputFile,
  [string]$PayloadDirectory,
  [string]$IconFile
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $PayloadDirectory) {
  $PayloadDirectory = Join-Path $repository 'chatgpt_temporaryfolder\installer-stage\payload'
}
if (-not $IconFile) { $IconFile = Join-Path $repository 'resource\weasel.ico' }
$payload = (Resolve-Path -LiteralPath $PayloadDirectory).Path.TrimEnd('\')
$compiler = (Resolve-Path -LiteralPath $MakeNsis).Path
$icon = (Resolve-Path -LiteralPath $IconFile).Path
$output = [IO.Path]::GetFullPath($OutputFile)
if ([IO.Path]::GetExtension($output) -ne '.exe') { throw 'OutputFile must end in .exe.' }
New-Item -ItemType Directory -Force -Path (Split-Path $output -Parent) | Out-Null

# Package only the reviewed application files and stock shared data. Never copy
# a user's Rime directory, registry export, sync data or installation identifier.
$required = @(
  '7-zip-license.txt', '7z.dll', '7z.exe', 'COPYING-curl.txt',
  'curl-ca-bundle.crt', 'curl.exe', 'LICENSE.txt', 'README.txt',
  'rime-install-config.bat', 'rime-install.bat', 'rime.dll',
  'start_service.bat', 'stop_service.bat', 'weasel.dll', 'weasel.ime',
  'WeaselDeployer.exe', 'WeaselServer.exe', 'WeaselSetup.exe',
  'weaselx64.dll', 'weaselx64.ime', 'WinSparkle.dll'
)
foreach ($name in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $payload $name) -PathType Leaf)) {
    throw "Missing required payload: $name"
  }
}
$files = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName)
if (Get-ChildItem -LiteralPath $payload -Recurse -Force |
    Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
  throw 'The payload must contain ordinary files and folders only.'
}

function Quote-Nsis([string]$Text) {
  return '"' + $Text.Replace('$', '$$').Replace('"', '$\"') + '"'
}

$install = [Collections.Generic.List[string]]::new()
$remove = [Collections.Generic.List[string]]::new()
$directories = [Collections.Generic.HashSet[string]]::new()
$manifest = @()
foreach ($file in $files) {
  $relative = $file.FullName.Substring($payload.Length + 1)
  if ($relative -match '[\r\n]') { throw "Invalid payload filename: $relative" }
  $sharedData = $relative -match '^data\\(?:[^\\]+\.(?:yaml|txt|gram)|opencc\\[^\\]+\.(?:json|ocd2?)|preview\\[^\\]+\.png)$'
  if ($relative -notin $required -and -not $sharedData) {
    throw "Unexpected payload file: $relative"
  }
  if ($relative -match '(?i)(?:^|\\)(?:installation|user)\.yaml$|\.custom\.yaml$|\.userdb(?:\.|$)') {
    throw "Personal configuration is not allowed in the installer: $relative"
  }
  $directory = Split-Path $relative -Parent
  $suffix = if ($directory) { '\' + $directory } else { '' }
  $install.Add('  SetOutPath "$INSTDIR' + $suffix + '"')
  $install.Add('  File ' + (Quote-Nsis $file.FullName))
  $remove.Add('  Delete /REBOOTOK "$INSTDIR\' + $relative + '"')
  while ($directory) {
    [void]$directories.Add($directory)
    $directory = Split-Path $directory -Parent
  }
  $manifest += [pscustomobject]@{
    path = $relative
    size = $file.Length
    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
  }
}
if (-not ($manifest.path -contains 'data\default.yaml') -or
    -not ($manifest.path -contains 'data\luna_pinyin.schema.yaml') -or
    -not ($manifest.path -contains 'data\luna_pinyin.dict.yaml') -or
    -not ($manifest.path -contains 'data\essay.txt') -or
    -not ($manifest.path -contains 'data\opencc\s2t.json')) {
  throw 'The stock offline input dictionaries or OpenCC data are incomplete.'
}

$stage = Join-Path $repository 'chatgpt_temporaryfolder\installer-stage'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$listFile = Join-Path $stage 'payload-files.nsh'
$lines = @('; Generated from the validated payload; do not edit.', '!macro InstallPayload') +
  $install.ToArray() + @('!macroend', '!macro RemovePayload') + $remove.ToArray()
foreach ($directory in @($directories) | Sort-Object Length -Descending) {
  $lines += '  RMDir /REBOOTOK "$INSTDIR\' + $directory + '"'
}
$lines += '!macroend'
[IO.File]::WriteAllLines($listFile, $lines, [Text.UTF8Encoding]::new($true))
$manifest | ConvertTo-Json -Depth 4 |
  Set-Content -LiteralPath (Join-Path $stage 'payload-manifest.json') -Encoding UTF8

& $compiler '/V3' "/DPAYLOAD_DIR=$payload" "/DOUTPUT_FILE=$output" "/DICON_FILE=$icon" "/DPAYLOAD_LIST=$listFile" (Join-Path $PSScriptRoot 'installer.nsi')
if ($LASTEXITCODE -ne 0) { throw "NSIS failed with exit code $LASTEXITCODE." }
Get-FileHash -LiteralPath $output -Algorithm SHA256
