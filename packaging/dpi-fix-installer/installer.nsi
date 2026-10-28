; Offline installer for the validated 0.16.3 runtime and the DPI-fixed TSF DLLs.
Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include WinVer.nsh
!include FileFunc.nsh
!include preflight.nsh

!ifndef PAYLOAD_DIR
  !error "Pass /DPAYLOAD_DIR with the reviewed application payload."
!endif
!ifndef OUTPUT_FILE
  !error "Pass /DOUTPUT_FILE with the destination EXE."
!endif
!ifndef ICON_FILE
  !error "Pass /DICON_FILE with the Weasel icon."
!endif
!include "${PAYLOAD_LIST}"

!define PACKAGE_ID "weasel-tray-dpi-fix-20261028"
!define PACKAGE_NAME "小狼毫 0.16.3 托盘图标修复版"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Weasel"
!define RIME_KEY "Software\Rime\Weasel"
!define SHORTCUT_DIR "小狼毫输入法"
Var CheckOnly

Name "${PACKAGE_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\Rime\weasel-0.16.3-dpi-fix"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetOverwrite on
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "0.16.3.2"
VIAddVersionKey /LANG=2052 "ProductName" "${PACKAGE_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "小狼毫离线安装程序（托盘 DPI 修复）"
VIAddVersionKey /LANG=2052 "FileVersion" "0.16.3.2"
VIAddVersionKey /LANG=2052 "LegalCopyright" "GPLv3; Rime Developers and contributors"
VIAddVersionKey /LANG=2052 "Comments" "Custom build 2026-10-28; x64 Windows 10/11"

!define MUI_ICON "${ICON_FILE}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "本安装包包含完整的小狼毫输入法和托盘‘中/英’图标缩放修复，可离线安装。$\r$\n$\r$\n适用于 Intel/AMD 64 位 Windows 10/11，同时支持 32 位应用。$\r$\n$\r$\n若已安装小狼毫，请先从 Windows 设置中卸载旧版本，再运行本安装程序。卸载旧版时请保留个人词库与用户文件夹。"
!define MUI_FINISHPAGE_TEXT "小狼毫已安装。按 Win+空格切换输入法。$\r$\n$\r$\n已打开的程序可能仍使用旧版输入法组件。请重新启动这些程序，或注销 Windows 后重新登录。$\r$\n$\r$\n本修复版默认关闭自动更新。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\WeaselServer.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动小狼毫"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOAD_DIR}\LICENSE.txt"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro DefineWeaselPreflightFunctions

Function .onInit
  StrCpy $CheckOnly 0
  ${GetParameters} $R1
  ClearErrors
  ${GetOptions} $R1 "/CHECKONLY" $R2
  ${IfNot} ${Errors}
    StrCpy $CheckOnly 1
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    IfSilent +2
      MessageBox MB_OK|MB_ICONSTOP "本安装包需要 Windows 10 或 Windows 11。"
    SetErrorLevel 1633
    Quit
  ${EndIf}
  ${IfNot} ${IsNativeAMD64}
    IfSilent +2
      MessageBox MB_OK|MB_ICONSTOP "本安装包适用于 Intel/AMD 64 位 Windows，不支持 ARM64 或 32 位 Windows。"
    SetErrorLevel 1633
    Quit
  ${EndIf}

  ; An uninstaller may leave COM metadata after removing the actual program.
  ; Only an existing runtime or uninstaller is evidence of an installed copy.
  SetRegView 32
  !insertmacro FindExistingWeasel HKLM "${RIME_KEY}" "${UNINSTALL_KEY}"
  StrCmp $R0 "" 0 existing_installation
  SetRegView 64
  !insertmacro FindExistingWeasel HKLM "${RIME_KEY}" "${UNINSTALL_KEY}"
  StrCmp $R0 "" 0 existing_installation
  SetRegView 32
  StrCpy $INSTDIR "$PROGRAMFILES64\Rime\weasel-0.16.3-dpi-fix"
  ${DirState} "$INSTDIR" $R0
  StrCmp $R0 1 occupied_directory ready

existing_installation:
  IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "检测到仍然存在的小狼毫程序：$\r$\n$R0$\r$\n$\r$\n请先通过 Windows‘设置 → 应用’卸载旧版，再运行本安装包。卸载时请保留个人用户文件夹。"
  SetErrorLevel 1638
  Quit
occupied_directory:
  IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "安装目标文件夹中已有文件：$\r$\n$INSTDIR$\r$\n$\r$\n如果旧版已卸载，请先将此文件夹改名保留，再重试。个人词库和配置无需删除。"
  SetErrorLevel 1638
  Quit
ready:
  ${If} $CheckOnly == 1
    ; Read-only diagnostic mode: do not enter the installation section.
    SetErrorLevel 0
    Quit
  ${EndIf}
FunctionEnd

Section "小狼毫输入法" InstallSection
  SectionIn RO
  SetShellVarContext all
  SetRegView 32
  ClearErrors
  !insertmacro InstallPayload
  IfErrors file_error
  SetOutPath "$INSTDIR"

  ; Install an uninstaller before registration so a partial install is removable.
  WriteINIStr "$INSTDIR\dpi-fix-package.ini" "Package" "Id" "${PACKAGE_ID}"
  WriteINIStr "$INSTDIR\dpi-fix-package.ini" "Package" "Source" "https://github.com/gaoqiangks/weasel/commit/dfdcb7baebb3c395105eee3b2bc47ac18169369d"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PACKAGE_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "0.16.3.2"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\WeaselServer.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Rime community (custom DPI fix)"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/gaoqiangks/weasel"
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  WriteRegStr HKLM "${RIME_KEY}" "InstallDir" "$PROGRAMFILES64\Rime"
  IfErrors file_error

  ; Keep an existing user's Chinese locale and custom user directory preference.
  ReadRegDWORD $1 HKCU "${RIME_KEY}" "Hant"
  StrCpy $2 "/s"
  ${If} $1 == 1
    StrCpy $2 "/t"
  ${Else}
    StrCpy $1 0
  ${EndIf}
  ClearErrors
  ExecWait '$\"$INSTDIR\WeaselSetup.exe$\" $2' $0
  ${If} ${Errors}
    SetErrorLevel 1
    Abort "无法启动输入法注册程序。请从 Windows 应用列表卸载本修复版后重新安装。"
  ${EndIf}
  ${If} $0 != 0
    DetailPrint "输入法注册失败，返回码 $0。"
    SetErrorLevel 1
    Abort "输入法注册失败。请从 Windows 应用列表卸载本修复版后重新安装。"
  ${EndIf}
  WriteRegDWORD HKCU "${RIME_KEY}" "Hant" $1
  ReadRegStr $0 HKLM "${RIME_KEY}" "WeaselRoot"
  StrCmp $0 "$INSTDIR" registration_ok
  SetErrorLevel 1
  Abort "输入法安装目录注册未通过验证。请卸载本修复版后重新安装。"
registration_ok:
  WriteRegStr HKCU "${RIME_KEY}\Updates" "CheckForUpdates" "0"

  CreateDirectory "$SMPROGRAMS\${SHORTCUT_DIR}"
  CreateShortCut "$SMPROGRAMS\${SHORTCUT_DIR}\输入法设定.lnk" "$INSTDIR\WeaselDeployer.exe"
  CreateShortCut "$SMPROGRAMS\${SHORTCUT_DIR}\重新部署.lnk" "$INSTDIR\WeaselDeployer.exe" "/deploy"
  CreateShortCut "$SMPROGRAMS\${SHORTCUT_DIR}\用户文件夹.lnk" "$INSTDIR\WeaselServer.exe" "/userdir"
  CreateShortCut "$SMPROGRAMS\${SHORTCUT_DIR}\启动小狼毫.lnk" "$INSTDIR\WeaselServer.exe"
  CreateShortCut "$SMPROGRAMS\${SHORTCUT_DIR}\卸载小狼毫.lnk" "$INSTDIR\uninstall.exe"

  IfSilent deploy_silently
    ClearErrors
    ExecWait '$\"$INSTDIR\WeaselDeployer.exe$\" /install' $0
    Goto deploy_finished
deploy_silently:
    ClearErrors
    ExecWait '$\"$INSTDIR\WeaselDeployer.exe$\" /deploy' $0
deploy_finished:
  ${If} ${Errors}
    SetErrorLevel 1
    Abort "无法启动输入方案部署程序。可从开始菜单重新运行‘输入法设定’。"
  ${EndIf}
  ${If} $0 != 0
    SetErrorLevel 1
    Abort "输入方案部署未完成。可从开始菜单重新运行‘输入法设定’。"
  ${EndIf}

  SetRegView 64
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer" '$\"$INSTDIR\WeaselServer.exe$\"'
  SetRegView 32
  IfSilent 0 done
    Exec '$\"$INSTDIR\WeaselServer.exe$\"'
done:
  SetErrorLevel 0
  Goto section_end
file_error:
  SetErrorLevel 1
  Abort "文件或安装信息写入失败。请检查磁盘空间和管理员权限。"
section_end:
SectionEnd

Function un.onInit
  SetRegView 32
  ReadINIStr $0 "$INSTDIR\dpi-fix-package.ini" "Package" "Id"
  StrCmp $0 "${PACKAGE_ID}" 0 not_our_installation
  ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
  StrCmp $0 "$INSTDIR" 0 not_our_installation
  ; Refuse to unregister a later installation owned by a different directory.
  ReadRegStr $0 HKLM "${RIME_KEY}" "WeaselRoot"
  StrCmp $0 "" ready_to_uninstall
  StrCmp $0 "$INSTDIR" ready_to_uninstall not_our_installation
not_our_installation:
  IfSilent +2
    MessageBox MB_OK|MB_ICONSTOP "当前输入法安装与本卸载程序不匹配，已停止操作。"
  SetErrorLevel 1
  Abort
ready_to_uninstall:
FunctionEnd

Section "Uninstall"
  SetShellVarContext all
  SetRegView 32
  ClearErrors
  ExecWait '$\"$INSTDIR\WeaselServer.exe$\" /quit' $0
  ${If} ${Errors}
    SetErrorLevel 1
    Abort "无法启动输入法停止程序，程序文件已保留。"
  ${EndIf}
  ClearErrors
  ExecWait '$\"$INSTDIR\WeaselSetup.exe$\" /u' $0
  ${If} ${Errors}
    SetErrorLevel 1
    Abort "无法启动输入法注销程序，程序文件已保留。"
  ${EndIf}
  ${If} $0 != 0
    SetErrorLevel 1
    Abort "输入法注销失败，程序文件已保留。请关闭使用输入法的程序后重试。"
  ${EndIf}
  SetRegView 64
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer"
  SetRegView 32
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "${RIME_KEY}"
  SetOutPath "$TEMP"
  !insertmacro RemovePayload
  Delete /REBOOTOK "$INSTDIR\dpi-fix-package.ini"
  Delete /REBOOTOK "$INSTDIR\uninstall.exe"
  RMDir /REBOOTOK "$INSTDIR"
  Delete "$SMPROGRAMS\${SHORTCUT_DIR}\输入法设定.lnk"
  Delete "$SMPROGRAMS\${SHORTCUT_DIR}\重新部署.lnk"
  Delete "$SMPROGRAMS\${SHORTCUT_DIR}\用户文件夹.lnk"
  Delete "$SMPROGRAMS\${SHORTCUT_DIR}\启动小狼毫.lnk"
  Delete "$SMPROGRAMS\${SHORTCUT_DIR}\卸载小狼毫.lnk"
  RMDir "$SMPROGRAMS\${SHORTCUT_DIR}"
  ; Leave HKCU preferences, APPDATA/Rime and any custom user directory intact.
  SetRebootFlag true
  SetErrorLevel 0
SectionEnd
