; NSIS Installer Script for VEX TM Remote Display Server
; Requires NSIS 3.0 or later

!define PRODUCT_NAME "VEX TM Remote Display Server"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "VEX Tournament Manager"
!define PRODUCT_WEB_SITE "https://github.com/steventine/vex-tm-remote-display"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\tm_stream_server.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; MUI 1.67 compatible ------
!include "MUI2.nsh"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Welcome page
!insertmacro MUI_PAGE_WELCOME
; License page (optional - add if you have a license file)
; !insertmacro MUI_PAGE_LICENSE "license.txt"
; Components page
!insertmacro MUI_PAGE_COMPONENTS
; Directory page
!insertmacro MUI_PAGE_DIRECTORY
; Instfiles page
!insertmacro MUI_PAGE_INSTFILES
; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\tm_stream_server.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run VEX TM Remote Display Server"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

; Language files
!insertmacro MUI_LANGUAGE "English"

; MUI end ------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "VEXTMRemoteDisplayServer-${PRODUCT_VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES\VEX TM Remote Display Server"
InstallDirRegKey HKCU "${PRODUCT_DIR_REGKEY}" ""
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

; Version Information
VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME}"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"

; Helper functions and macros (must be defined before use)
Function StrStr
    Exch $R1 ; st=haystack,old$R1, $R1=needle
    Exch    ; st=old$R1,haystack, $R1=needle
    Exch $R2 ; st=old$R1,old$R2, $R2=haystack, $R1=needle
    Push $R3
    Push $R4
    Push $R5
    StrLen $R3 $R1
    StrCpy $R4 0
    loop:
        StrCpy $R5 $R2 $R3 $R4
        StrCmp $R5 $R1 done
        StrCmp $R5 "" done
        IntOp $R4 $R4 + 1
        Goto loop
    done:
        StrCpy $R1 $R2 "" $R4
        Pop $R5
        Pop $R4
        Pop $R3
        Pop $R2
        Exch $R1
FunctionEnd

!macro StrStr ResultVar String SubString
    Push "${SubString}"
    Push "${String}"
    Call StrStr
    Pop "${ResultVar}"
!macroend
!define StrStr '!insertmacro StrStr'

Function StrRep
    Exch $R4 ; st=old$R4, $R4=replace
    Exch    ; st=$R4,old$R4
    Exch $R3 ; st=$R4,old$R3, $R3=old$R4, $R4=replace
    Exch 2   ; st=old$R3,$R4,old$R4, $R3=replace
    Exch $R1 ; st=old$R3,$R4,old$R1, $R1=old$R4, $R3=replace, $R4=string
    Exch 2   ; st=old$R1,$R4,old$R3, $R1=string, $R3=replace, $R4=search
    Exch $R2 ; st=old$R1,$R4,old$R2, $R2=old$R3, $R1=string, $R3=replace, $R4=search
    Push $R5
    Push $R6
    Push $R7
    Push $R8
    Push $R9
    StrCpy $R9 $R1
    StrLen $R7 $R4
    StrCpy $R8 0
    loop:
        StrCpy $R5 $R9 $R7 $R8
        StrCmp $R5 $R4 found
        StrCmp $R5 "" done
        IntOp $R8 $R8 + 1
        Goto loop
    found:
        StrCpy $R5 $R9 $R8
        IntOp $R6 $R8 + $R7
        StrCpy $R6 $R9 "" $R6
        StrCpy $R9 "$R5$R3$R6"
        IntOp $R8 $R8 + $R7
        Goto loop
    done:
        StrCpy $R1 $R9
        Pop $R9
        Pop $R8
        Pop $R7
        Pop $R6
        Pop $R5
        Pop $R2
        Pop $R4
        Pop $R3
        Exch $R1
FunctionEnd

!macro StrRep ResultVar String Search Replace
    Push "${String}"
    Push "${Search}"
    Push "${Replace}"
    Call StrRep
    Pop "${ResultVar}"
!macroend
!define StrRep '!insertmacro StrRep'

Section "Main Application" SecMain
    SectionIn RO
    
    SetOutPath "$INSTDIR"
    
    ; Install main executable
    File "tm_stream_server.exe"
    
    ; Install platform files (if needed)
    ; File "platform-windows.dll"  ; if using DLLs
    
    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
    
    ; Create Start Menu shortcuts
    CreateDirectory "$SMPROGRAMS\VEX TM Remote Display Server"
    CreateShortCut "$SMPROGRAMS\VEX TM Remote Display Server\VEX TM Remote Display Server.lnk" "$INSTDIR\tm_stream_server.exe"
    CreateShortCut "$SMPROGRAMS\VEX TM Remote Display Server\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    
    ; Create desktop shortcut (optional)
    ; CreateShortCut "$DESKTOP\VEX TM Remote Display Server.lnk" "$INSTDIR\tm_stream_server.exe"
    
    ; Registry entries
    WriteRegStr HKCU "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\tm_stream_server.exe"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name)"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\tm_stream_server.exe"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\VEX TM Remote Display Server.lnk" "$INSTDIR\tm_stream_server.exe"
SectionEnd

Section "Add to PATH" SecPath
    ; Add to system PATH using registry
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    StrCpy $1 "$0;$INSTDIR"
    
    ; Check if already in PATH
    ${StrStr} $2 "$0" "$INSTDIR"
    StrCmp $2 "" 0 skip_path_add
        WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$1"
        DetailPrint "Added to PATH: $INSTDIR"
        ; Broadcast environment change
        SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    skip_path_add:
SectionEnd

; Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Main application files (required)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a desktop shortcut"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPath} "Add installation directory to system PATH"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
    ; Check if already installed
    ReadRegStr $R0 ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString"
    StrCmp $R0 "" done
    
    MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
    "${PRODUCT_NAME} is already installed. $\n$\nClick `OK` to remove the \
    previous version or `Cancel` to cancel this upgrade." \
    IDOK uninst
    Abort
    
    uninst:
        ClearErrors
        ExecWait '$R0 _?=$INSTDIR'
        
        IfErrors no_remove_uninstaller done
        no_remove_uninstaller:
    
    done:
FunctionEnd

Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\tm_stream_server.exe"
    Delete "$INSTDIR\uninstall.exe"
    
    ; Remove shortcuts
    Delete "$SMPROGRAMS\VEX TM Remote Display Server\VEX TM Remote Display Server.lnk"
    Delete "$SMPROGRAMS\VEX TM Remote Display Server\Uninstall.lnk"
    RMDir "$SMPROGRAMS\VEX TM Remote Display Server"
    Delete "$DESKTOP\VEX TM Remote Display Server.lnk"
    
    ; Remove from PATH
    ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    ${StrStr} $1 "$0" "$INSTDIR"
    StrCmp $1 "" skip_path_remove
        ; Remove the path entry
        ${StrRep} $0 "$0" "$INSTDIR;" ""
        ${StrRep} $0 "$0" ";$INSTDIR" ""
        ${StrRep} $0 "$0" "$INSTDIR" ""
        WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
        ; Broadcast environment change
        SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    skip_path_remove:
    
    ; Remove registry keys
    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
    DeleteRegKey HKCU "${PRODUCT_DIR_REGKEY}"
    
    ; Remove installation directory
    RMDir "$INSTDIR"
    
    ; Show message
    MessageBox MB_ICONINFORMATION|MB_OK "$(^Name) was successfully removed from your computer."
SectionEnd

