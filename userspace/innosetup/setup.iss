; Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>

#if Ver < EncodeVer(7,1,0,0)
        #error This script requires Inno Setup 7.1.0 or later
#endif

#ifndef SolutionDir
        #error Use option /DSolutionDir=<path>
#endif

#ifdef Platform
        #define Platform Lowercase(Platform)
#else
        #error Use option /DPlatform=<platform>
#endif

#ifdef Configuration
        #define Configuration Lowercase(Configuration)
#else
        #error Use option /DConfiguration=<cfg>
#endif

#ifdef ExePath
        #define BuildDir AddBackslash(ExtractFilePath(ExePath))
#else
        #error Use option /DExePath=path-to-exe
#endif

#ifndef GuiExePath
        #error Use option /DGuiExePath=path-to-exe
#endif

#ifndef VCToolsRedistInstallDir
        #error Use option /DVCToolsRedistInstallDir
#endif

#define VCToolsRedistExe "vc_redist." + Platform + ".exe"

#define AppExeName ExtractFileName(ExePath)
#define GuiExeName ExtractFileName(GuiExePath)

; information from .exe GetVersionInfo
#define ProductName GetStringFileInfo(ExePath, PRODUCT_NAME)
#define AppVersion GetVersionNumbersString(ExePath)
#define Copyright GetFileCopyrightString(ExePath)
#define Company GetFileCompanyString(ExePath)

#define AppGUID "{199505b0-b93d-4521-a8c7-897818e0205a}"
#define TaskDetachAll "USBip Detach All On Reboot Or Shutdown"

#define FilterDriver "usbip2_filter"
#define UdeDriver "usbip2_ude"

#define CLIENT_HWID "ROOT\USBIP_WIN2\UDE"

; Project's test certificate is no longer installed for github releases since 0.9.7.5.
; But it is required during the development.
#define CertFileName "usbip.pfx"
#define CertFilePath SolutionDir + "drivers\package\" + CertFileName
#define CertName "USBip"
#define CertPwd "usbip"

; whether drivers are signed by the project's test certificate
#define TEST_SIGNED_DRIVERS

#define INSTALL_TEST_CERTIFICATE Defined(TEST_SIGNED_DRIVERS)

#if Platform == "arm64"
  #define ArchMode "arm64"
  #define VCRedistArch "ARM64"
#else
  #define ArchMode "x64os"
  #define VCRedistArch "x64"
#endif

[Setup]
AppName={#ProductName}
AppVersion={#AppVersion}
AppCopyright={#Copyright}
AppPublisher={#Company}
AppPublisherURL=https://github.com/vadimgrn/usbip-win2
WizardStyle=modern
DefaultDirName={autopf}\{#ProductName}
DefaultGroupName={#ProductName}
ArchitecturesAllowed={#ArchMode}
ArchitecturesInstallIn64BitMode={#ArchMode}
VersionInfoVersion={#AppVersion}
ShowLanguageDialog=no
AllowNoIcons=yes
LicenseFile={#SolutionDir + "LICENSE.txt"}
AppId={{#AppGUID}
OutputBaseFilename={#ProductName}-{#AppVersion}-{#Platform}-{#Configuration}
OutputDir={#BuildDir}
SolidCompression=yes
DisableWelcomePage=no
WizardSmallImageFile=48.bmp,64.bmp,128.bmp
WizardImageFile=164.bmp,192.bmp
WizardImageAlphaFormat=defined
WizardImageStretch=no
UninstallDisplayIcon="{app}\{#AppExeName}"
AlwaysRestart=yes

; this app can't be installed more than once
MissingRunOnceIdsWarning=no

; Windows 10, version 1903
MinVersion=10.0.18362

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.

[Components]
Name: "main"; Description: "Main Files"; Types: full compact custom; Flags: fixed
Name: "client"; Description: "Client"; Types: full compact custom; Flags: fixed
Name: "gui"; Description: "GUI"; Types: full
Name: "sdk"; Description: "USBIP Software Development Kit"; Types: full
Name: "pdb"; Description: "Program DataBase files"; Types: full

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\{#GuiExeName}"; Components: gui
Name: "{group}\{cm:UninstallProgram,{#ProductName}}"; Filename: "{uninstallexe}"; Components: main
Name: "{commondesktop}\{#ProductName}"; Filename: "{app}\{#GuiExeName}"; Tasks: desktopicon

[Files]

Source: {#SolutionDir + "Readme.md"}; DestDir: "{app}"; Flags: isreadme; Components: main

Source: {#BuildDir + "usbip.exe"}; DestDir: "{app}"; Components: main
Source: {#BuildDir + "devnode.exe"}; DestDir: "{app}"; Components: main
Source: {#BuildDir + "*.dll"}; DestDir: "{app}"; Components: main

Source: {#SolutionDir + "userspace\libusbip\*.h"}; DestDir: "{app}\include\usbip"; Excludes: "resource.h"; Components: sdk
Source: {#SolutionDir + "userspace\resources\messages.h"}; DestDir: "{app}\include\usbip"; Components: sdk
Source: {#BuildDir + "libusbip.lib"}; DestDir: "{app}\lib"; Components: sdk

Source: {#BuildDir + "*.pdb"}; DestDir: "{app}"; Excludes: "libusbip*.pdb, wusbip.pdb"; Components: pdb
Source: {#BuildDir + "libusbip.pdb"}; DestDir: "{app}"; Components: pdb or sdk
; Source: {#BuildDir + "wusbip.pdb"}; DestDir: "{app}"; Components: pdb and gui
; wusbip.pdb is too large

Source: {#BuildDir + "wusbip.exe"}; DestDir: "{app}"; Components: gui

Source: {#VCToolsRedistInstallDir}{#VCToolsRedistExe}; DestDir: "{tmp}"; Flags: nocompression; Components: main
Source: {#BuildDir + "package\*"}; DestDir: "{tmp}"; Components: main
Source: {#SolutionDir + "userspace\innosetup\task_detach_all.xml"}; DestDir: "{tmp}"; Components: client

#if INSTALL_TEST_CERTIFICATE
  Source: {#CertFilePath}; DestDir: "{tmp}"; Components: main
#endif

[Tasks]
Name: vcredist; Description: "Install Microsoft Visual C++ &Redistributable ({#VCRedistArch})"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: gui

[Run]

Filename: {tmp}\{#VCToolsRedistExe}; Parameters: "/quiet /norestart"; Tasks: vcredist

#if INSTALL_TEST_CERTIFICATE
  Filename: {sys}\certutil.exe; Parameters: "-f -p ""{#CertPwd}"" -importPFX root ""{tmp}\{#CertFileName}"" FriendlyName=""{#CertName}"""; Flags: runhidden
#endif

Filename: {sys}\pnputil.exe; Parameters: "/add-driver ""{tmp}\{#FilterDriver}.inf"" /install"; Flags: runhidden; Components: client
Filename: {app}\devnode.exe; Parameters: "install ""{tmp}\{#UdeDriver}.inf"" {#CLIENT_HWID}"; Flags: runhidden; Components: client

Filename: {sys}\schtasks.exe; Parameters: "/create /tn ""{#TaskDetachAll}"" /f /xml ""{tmp}\task_detach_all.xml"""; Flags: runhidden; Components: client

[UninstallRun]

Filename: {sys}\schtasks.exe; Parameters: "/delete /tn ""{#TaskDetachAll}"" /f"; Flags: runhidden; Components: client
Filename: {app}\devnode.exe; Parameters: "remove {#CLIENT_HWID} root"; Flags: runhidden; Components: client

#if INSTALL_TEST_CERTIFICATE
  Filename: {sys}\certutil.exe; Parameters: "-f -delstore root ""{#CertName}"""; Flags: runhidden
#endif

[Code]

function make_bcd_subkey_path(const object, element: String): String;
begin
  result := 'BCD00000000\Objects\' + object +  '\Elements\' + element;
end;

function IsTestSigningModeEnabled(): Boolean;
var
  subkey, name, value : String;
  binval : AnsiString;
begin
  subkey := make_bcd_subkey_path('{9DEA862C-5CDD-4E70-ACC1-F32B344D4795}', '23000003'); // default loader
  name := 'Element';
  
  result := RegQueryStringValue(HKEY_LOCAL_MACHINE, subkey, name, value);
  if not result then
    exit;
   
  subkey := make_bcd_subkey_path(value, '16000049'); // AllowPrereleaseSignatures
  result := RegQueryBinaryValue(HKEY_LOCAL_MACHINE, subkey, name, binval) and (Length(binval) >= 1) and (binval[1] = #1)
end;

function check_test_sign_mode(): Boolean;
begin
#ifdef TEST_SIGNED_DRIVERS
  result := IsTestSigningModeEnabled();
  if not result then
    MsgBox('To use USBip, enable test-signed drivers to load.' #13#13
           'Run "Bcdedit.exe -set TESTSIGNING ON" as Administrator and reboot the PC.',
            mbCriticalError, MB_OK);
#else
  result := true;
#endif
end;

function InitializeSetup(): Boolean;
begin
  result := check_test_sign_mode();
end;

procedure InitializeWizard();
begin
  WizardForm.LicenseAcceptedRadio.Checked := True;
end;

procedure UpdateDetachTaskXml();
var
  TaskFile: String;
  Lines: TArrayOfString;
  I: Integer;
begin
  TaskFile := ExpandConstant('{tmp}\task_detach_all.xml');
  if FileExists(TaskFile) and LoadStringsFromFile(TaskFile, Lines) then
  begin
    for I := 0 to GetArrayLength(Lines) - 1 do
      StringChange(Lines[I], 'USBIP_DIR', ExpandConstant('{app}'));
    SaveStringsToUTF8FileWithoutBOM(TaskFile, Lines, False);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    UpdateDetachTaskXml();
  end;
end;

procedure DeleteOemDriver(const DriverName: String);
var
  FindRec: TFindRec;
  InfDir: String;
  Lines: TArrayOfString;
  I, ResultCode: Integer;
  Matched: Boolean;
begin
  InfDir := ExpandConstant('{win}\INF\');
  if FindFirst(InfDir + 'oem*.inf', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          Matched := False;
          if LoadStringsFromFile(InfDir + FindRec.Name, Lines) then
          begin
            for I := 0 to GetArrayLength(Lines) - 1 do
            begin
              if Pos(DriverName, Lines[I]) > 0 then
              begin
                Matched := True;
                Break;
              end;
            end;
          end;
          if Matched then
          begin
            Log('Deleting OEM driver ' + FindRec.Name + ' (' + DriverName + ')');
            Exec(ExpandConstant('{sys}\pnputil.exe'),
                 '/delete-driver ' + FindRec.Name + ' /uninstall /force',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DeleteOemDriver('{#UdeDriver}');
    DeleteOemDriver('{#FilterDriver}');
  end;
end;

function UninstallNeedRestart(): Boolean;
begin
  result := true;
end;

// Check if an existing version of USBip is installed and locate its uninstaller
function GetInstalledUninstallString(var UninstPath, UninstParams: String): Boolean;
var
  SubKey, UninstStr: String;
  P: Integer;
begin
  Result := False;
  SubKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#AppGUID}_is1';

  // Check 64-bit HKLM, then 32-bit HKLM, then HKCU
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE_64, SubKey, 'UninstallString', UninstStr) then
    if not RegQueryStringValue(HKEY_LOCAL_MACHINE_32, SubKey, 'UninstallString', UninstStr) then
      if not RegQueryStringValue(HKEY_CURRENT_USER, SubKey, 'UninstallString', UninstStr) then
        Exit;

  UninstStr := Trim(UninstStr);
  if UninstStr = '' then
    Exit;

  // Extract executable path from potentially quoted UninstallString
  if (Length(UninstStr) > 0) and (UninstStr[1] = '"') then
  begin
    Delete(UninstStr, 1, 1);
    P := Pos('"', UninstStr);
    if P > 0 then
      UninstPath := Copy(UninstStr, 1, P - 1)
    else
      UninstPath := UninstStr;
  end
  else
  begin
    P := Pos(' ', UninstStr);
    if P > 0 then
      UninstPath := Copy(UninstStr, 1, P - 1)
    else
      UninstPath := UninstStr;
  end;

  UninstParams := '/SILENT /NORESTART /SUPPRESSMSGBOXES';
  Result := FileExists(UninstPath);
end;

function IsPackageInstalled(): Boolean;
var
  DummyPath, DummyParams: String;
begin
  Result := GetInstalledUninstallString(DummyPath, DummyParams);
  if Result then
    Log('Previous package detected as installed')
  else
    Log('Previous package not detected as installed');
end;

function UninstallPreviousPackage(): Integer;
var
  UninstPath, UninstParams: String;
begin
  Result := 1;
  if GetInstalledUninstallString(UninstPath, UninstParams) then
  begin
    Log('Uninstalling previous package: ' + UninstPath + ' ' + UninstParams);
    if Exec(UninstPath, UninstParams, '', SW_HIDE, ewWaitUntilTerminated, Result) then
    begin
      if Result = 0 then
        Log('Installed package uninstall completed successfully')
      else
        Log('Installed package uninstall did not complete successfully, exit code: ' + IntToStr(Result));
    end
    else
    begin
      Log('Failed to execute uninstaller: ' + SysErrorMessage(Result));
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): string;
begin
  result := '';
  if IsPackageInstalled() then
  begin
    if UninstallPreviousPackage() <> 0 then
      result := 'Failed to automatically uninstall the previous version of USBip. ' +
                'Please uninstall it manually before continuing.';
  end;
end;
