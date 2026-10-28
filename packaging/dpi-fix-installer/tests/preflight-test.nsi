Unicode true
RequestExecutionLevel user
SilentInstall silent
!include FileFunc.nsh
!include "..\preflight.nsh"
!ifndef TEST_EXE
  !error "Pass /DTEST_EXE with the scratch executable path."
!endif
OutFile "${TEST_EXE}"
!insertmacro DefineWeaselPreflightFunctions

Function .onInit
  ${GetParameters} $R2
  ${GetOptions} $R2 "/KEY=" $R3
  StrLen $R4 "Software\Codex\WeaselInstallerPreflightTests\"
  StrCpy $R5 $R3 $R4
  StrCmp $R5 "Software\Codex\WeaselInstallerPreflightTests\" 0 bad_arguments
  ${GetOptions} $R2 "/VIEW=" $R4
  StrCmp $R4 "64" 0 use_32_view
  SetRegView 64
  Goto run_check
use_32_view:
  SetRegView 32
run_check:
  !insertmacro FindExistingWeasel HKCU "$R3\Rime" "$R3\Uninstall"
  WriteINIStr "$EXEDIR\result.ini" "Result" "Evidence" $R0
  StrCmp $R0 "" no_installation
  SetErrorLevel 1638
  Quit
no_installation:
  SetErrorLevel 0
  Quit
bad_arguments:
  SetErrorLevel 2
  Quit
FunctionEnd

Section
SectionEnd
