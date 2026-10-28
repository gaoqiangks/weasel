!ifndef WEASEL_INSTALLER_PREFLIGHT_INCLUDED
!define WEASEL_INSTALLER_PREFLIGHT_INCLUDED
!include LogicLib.nsh

; Return an existing program file in $R0, or an empty string. Registry values
; left behind by uninstall are not evidence that the application still exists.
; The caller selects the registry view; only $R0 and $R1 are scratch registers.
!macro FindExistingWeasel ROOT RIME_KEY UNINSTALL_KEY
  StrCpy $R0 ""
  ReadRegStr $R1 ${ROOT} "${RIME_KEY}" "WeaselRoot"
  ${If} $R1 != ""
    Push "$R1\WeaselServer.exe"
    Call WeaselPreflightExistingFile
    Pop $R0
  ${EndIf}
  ${If} $R0 == ""
    ReadRegStr $R1 ${ROOT} "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $R1 != ""
      Push "$R1\WeaselServer.exe"
      Call WeaselPreflightExistingFile
      Pop $R0
    ${EndIf}
  ${EndIf}
  ${If} $R0 == ""
    ReadRegStr $R1 ${ROOT} "${UNINSTALL_KEY}" "UninstallString"
    Push $R1
    Call WeaselPreflightUninstaller
    Pop $R0
  ${EndIf}
!macroend

!macro DefineWeaselPreflightFunctions
Function WeaselPreflightExistingFile
  Exch $0
  Push $1
  Push $2
  ExpandEnvStrings $0 $0
  StrCmp $0 "" preflight_file_done
  System::Call 'kernel32::GetFileAttributesW(w r0) i .r1'
  IntCmp $1 -1 preflight_file_missing
  IntOp $2 $1 & 0x10
  IntCmp $2 0 preflight_file_done
preflight_file_missing:
  StrCpy $0 ""
preflight_file_done:
  Pop $2
  Pop $1
  Exch $0
FunctionEnd

; Resolve the executable portion only; never execute a registry command.
; Accept quoted paths with arguments, plain complete paths, and conventional
; unquoted .exe paths with arguments. Every candidate must be an existing file.
Function WeaselPreflightUninstaller
  Exch $0
  Push $1
  Push $2
  Push $3
  Push $4
  ExpandEnvStrings $0 $0
preflight_trim_left:
  StrCpy $1 $0 1
  StrCmp $1 " " preflight_skip_space
  StrCmp $1 "$\t" preflight_skip_space preflight_parse_command
preflight_skip_space:
  StrCpy $0 $0 "" 1
  Goto preflight_trim_left
preflight_parse_command:
  StrCmp $1 '$\"' preflight_quoted
  Push $0
  Call WeaselPreflightExistingFile
  Pop $4
  StrCmp $4 "" 0 preflight_uninstaller_found
  StrCpy $1 ""
  StrCpy $2 0
preflight_unquoted_loop:
  StrCpy $3 $0 1 $2
  StrCmp $3 "" preflight_uninstaller_missing
  IntOp $2 $2 + 1
  StrCpy $1 "$1$3"
  StrCpy $3 $1 4 -4
  StrCmp $3 ".exe" 0 preflight_unquoted_loop
  StrCpy $3 $0 1 $2
  StrCmp $3 " " preflight_unquoted_candidate
  StrCmp $3 "$\t" preflight_unquoted_candidate
  StrCmp $3 "" preflight_unquoted_candidate preflight_unquoted_loop
preflight_unquoted_candidate:
  Push $1
  Call WeaselPreflightExistingFile
  Pop $4
  StrCmp $4 "" preflight_unquoted_loop preflight_uninstaller_found
preflight_quoted:
  StrCpy $1 ""
  StrCpy $2 1
preflight_quoted_loop:
  StrCpy $3 $0 1 $2
  StrCmp $3 "" preflight_uninstaller_missing
  StrCmp $3 '$\"' preflight_quoted_candidate
  StrCpy $1 "$1$3"
  IntOp $2 $2 + 1
  Goto preflight_quoted_loop
preflight_quoted_candidate:
  Push $1
  Call WeaselPreflightExistingFile
  Pop $4
  StrCmp $4 "" preflight_uninstaller_missing preflight_uninstaller_found
preflight_uninstaller_missing:
  StrCpy $4 ""
preflight_uninstaller_found:
  StrCpy $0 $4
  Pop $4
  Pop $3
  Pop $2
  Pop $1
  Exch $0
FunctionEnd
!macroend
!endif
