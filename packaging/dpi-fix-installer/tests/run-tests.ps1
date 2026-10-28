[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$MakeNsis)

$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$runId = [Guid]::NewGuid().ToString('N')
$scratch = Join-Path $repository "chatgpt_temporaryfolder\installer-preflight-tests\$runId"
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$compiler = (Resolve-Path -LiteralPath $MakeNsis).Path
$executable = Join-Path $scratch 'preflight-test.exe'
& $compiler '/V2' "/DTEST_EXE=$executable" (Join-Path $PSScriptRoot 'preflight-test.nsi')
if ($LASTEXITCODE -ne 0) { throw "NSIS test compilation failed: $LASTEXITCODE" }

$runtime = Join-Path $scratch 'active runtime'
$uninstallerDirectory = Join-Path $scratch 'uninstaller with spaces'
$directoryRuntime = Join-Path $scratch 'directory not executable'
New-Item -ItemType Directory -Path $runtime, $uninstallerDirectory, (Join-Path $directoryRuntime 'WeaselServer.exe') -Force | Out-Null
$server = Join-Path $runtime 'WeaselServer.exe'
$uninstaller = Join-Path $uninstallerDirectory 'uninstall.exe'
$orphanDll = Join-Path $scratch 'orphan-weasel.dll'
foreach ($file in @($server, $uninstaller, $orphanDll)) {
  Set-Content -LiteralPath $file -Value 'Test fixture; never executed.' -Encoding ASCII
}
$missing = Join-Path $scratch 'missing runtime'
$missingUninstaller = Join-Path $missing 'uninstall.exe'
$previousFixtureVariable = $env:WEASEL_PREFLIGHT_FIXTURE_ROOT
$env:WEASEL_PREFLIGHT_FIXTURE_ROOT = $scratch
$keyPrefix = "Software\Codex\WeaselInstallerPreflightTests\$runId"
$keyRoot = 'Registry::HKEY_CURRENT_USER\' + $keyPrefix
$results = [Collections.Generic.List[object]]::new()

# HKCU test keys may be shared across registry views. Use independent case keys;
# these cases exercise the predicates in both caller contexts, not WOW64 storage.
$cases = @(
  @{ Name = 'fresh'; Values = @{}; Evidence = '' },
  @{ Name = 'orphan-com-missing-dll'; Values = @{ 'Com|' = (Join-Path $scratch 'missing.dll') }; Evidence = '' },
  @{ Name = 'orphan-com-existing-dll'; Values = @{ 'Com|' = $orphanDll }; Evidence = '' },
  @{ Name = 'stale-runtime-root'; Values = @{ 'Rime|WeaselRoot' = $missing }; Evidence = '' },
  @{ Name = 'stale-uninstall-metadata'; Values = @{ 'Uninstall|InstallLocation' = $missing; 'Uninstall|UninstallString' = ('"' + $missingUninstaller + '" /S') }; Evidence = '' },
  @{ Name = 'runtime-root'; Values = @{ 'Rime|WeaselRoot' = $runtime }; Evidence = $server },
  @{ Name = 'uninstall-install-location'; Values = @{ 'Uninstall|InstallLocation' = $runtime }; Evidence = $server },
  @{ Name = 'quoted-uninstaller-with-arguments'; Values = @{ 'Uninstall|UninstallString' = ('"' + $uninstaller + '" /S') }; Evidence = $uninstaller },
  @{ Name = 'plain-uninstaller-with-spaces'; Values = @{ 'Uninstall|UninstallString' = $uninstaller }; Evidence = $uninstaller },
  @{ Name = 'unquoted-uninstaller-with-arguments'; Values = @{ 'Uninstall|UninstallString' = ($uninstaller + ' /S') }; Evidence = $uninstaller },
  @{ Name = 'environment-uninstaller'; Values = @{ 'Uninstall|UninstallString' = '"%WEASEL_PREFLIGHT_FIXTURE_ROOT%\uninstaller with spaces\uninstall.exe" /S' }; Evidence = $uninstaller },
  @{ Name = 'directory-is-not-a-server'; Values = @{ 'Rime|WeaselRoot' = $directoryRuntime }; Evidence = '' },
  @{ Name = 'directory-is-not-an-uninstaller'; Values = @{ 'Uninstall|UninstallString' = ('"' + (Join-Path $directoryRuntime 'WeaselServer.exe') + '" /S') }; Evidence = '' },
  @{ Name = 'unterminated-uninstaller-quote'; Values = @{ 'Uninstall|UninstallString' = ('"' + $uninstaller) }; Evidence = '' },
  @{ Name = 'stale-root-valid-uninstaller'; Values = @{ 'Rime|WeaselRoot' = $missing; 'Uninstall|UninstallString' = ('"' + $uninstaller + '" /S') }; Evidence = $uninstaller },
  @{ Name = 'leading-whitespace'; Values = @{ 'Uninstall|UninstallString' = (" `t" + '"' + $uninstaller + '" /S') }; Evidence = $uninstaller }
)

try {
  foreach ($view in @(32, 64)) {
    foreach ($case in $cases) {
      $relativeKey = "$keyPrefix\view$view\$($case.Name)"
      foreach ($entry in $case.Values.GetEnumerator()) {
        $part = $entry.Key.Split('|', 2)
        $subkey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($relativeKey + '\' + $part[0])
        try { $subkey.SetValue($part[1], $entry.Value, [Microsoft.Win32.RegistryValueKind]::String) }
        finally { $subkey.Dispose() }
      }
      $process = Start-Process -FilePath $executable -ArgumentList ('/KEY="{0}" /VIEW={1}' -f $relativeKey, $view) -WindowStyle Hidden -Wait -PassThru
      $expectedExit = if ($case.Evidence) { 1638 } else { 0 }
      $resultText = Get-Content -LiteralPath (Join-Path $scratch 'result.ini') -Raw
      $match = [regex]::Match($resultText, '(?m)^Evidence=(.*)\r?$')
      $evidence = if ($match.Success) { $match.Groups[1].Value.TrimEnd("`r") } else { $null }
      $passed = $process.ExitCode -eq $expectedExit -and $evidence -eq $case.Evidence
      $results.Add([pscustomobject]@{ view = $view; name = $case.Name; passed = $passed; exitCode = $process.ExitCode; evidence = $evidence })
      if (-not $passed) {
        throw "Case $($case.Name), view $view failed: exit $($process.ExitCode), evidence '$evidence'; expected $expectedExit, '$($case.Evidence)'."
      }
      Write-Output "PASS view=$view $($case.Name)"
    }
  }
} finally {
  # Remove only the unique registry fixtures created by this test run.
  if (Test-Path -LiteralPath $keyRoot) { Remove-Item -LiteralPath $keyRoot -Recurse -Force }
  $env:WEASEL_PREFLIGHT_FIXTURE_ROOT = $previousFixtureVariable
  $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $scratch 'results.json') -Encoding UTF8
}
Write-Output "PASS: $($results.Count) native NSIS checks. Results: $scratch\results.json"
