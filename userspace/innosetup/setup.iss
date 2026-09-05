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
#define DetachTaskName "USBip Detach All On Reboot Or Shutdown"

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
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
PrivilegesRequired=admin

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

#if INSTALL_TEST_CERTIFICATE
  Source: {#CertFilePath}; DestDir: "{tmp}"; Components: main
#endif

[Tasks]
Name: vcredist; Description: "Install Microsoft Visual C++ &Redistributable ({#VCRedistArch})"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: gui

[Run]

Filename: {tmp}\{#VCToolsRedistExe}; Parameters: "/quiet /norestart"; Tasks: vcredist; StatusMsg: "Installing Microsoft Visual C++ Redistributable ({#VCRedistArch})..."

#if INSTALL_TEST_CERTIFICATE
  Filename: {sys}\certutil.exe; Parameters: "-f -p ""{#CertPwd}"" -importPFX root ""{tmp}\{#CertFileName}"" FriendlyName=""{#CertName}"""; Flags: runhidden; StatusMsg: "Installing test certificate..."
#endif

Filename: {sys}\pnputil.exe; Parameters: "/add-driver ""{tmp}\{#FilterDriver}.inf"" /install"; Flags: runhidden; Components: client; StatusMsg: "Installing upper filter driver..."
Filename: {app}\devnode.exe; Parameters: "install ""{tmp}\{#UdeDriver}.inf"" {#CLIENT_HWID}"; Flags: runhidden; Components: client; StatusMsg: "Installing UDE driver and virtual host controller..."

[UninstallRun]

Filename: {app}\devnode.exe; Parameters: "remove {#CLIENT_HWID} root"; Flags: runhidden; Components: client; StatusMsg: "Removing virtual host controller device..."

#if INSTALL_TEST_CERTIFICATE
  Filename: {sys}\certutil.exe; Parameters: "-f -delstore root ""{#CertName}"""; Flags: runhidden; StatusMsg: "Removing test certificate..."
#endif

[Code]

type
  SYSTEM_CODEINTEGRITY_INFORMATION = record
    Length: Cardinal;
    CodeIntegrityOptions: Cardinal;
  end;

function NtQuerySystemInformation(
  SystemInformationClass: Integer;
  var SystemInformation: SYSTEM_CODEINTEGRITY_INFORMATION;
  SystemInformationLength: Cardinal;
  var ReturnLength: Cardinal
): Integer; external 'NtQuerySystemInformation@ntdll.dll stdcall';

const
  SystemCodeIntegrityInformation = 103;
  CODEINTEGRITY_OPTION_TESTSIGN = 2;

function IsTestSigningModeEnabled(): Boolean;
var
  Info: SYSTEM_CODEINTEGRITY_INFORMATION;
  RetLen: Cardinal;
  Status: Integer;
begin
  Result := False;
  Info.Length := 8;
  Info.CodeIntegrityOptions := 0;
  RetLen := 0;

  Status := NtQuerySystemInformation(SystemCodeIntegrityInformation, Info, 8, RetLen);
  if Status = 0 then
  begin
    Result := (Info.CodeIntegrityOptions and CODEINTEGRITY_OPTION_TESTSIGN) <> 0;
  end;
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

procedure RegisterDetachTask();
var
  Scheduler, RootFolder, TaskDef, Trigger, Principal, Settings, Action: Variant;
begin
  Log('Registering detach task: {#DetachTaskName}');
  try
    Scheduler := CreateOleObject('Schedule.Service');
    Scheduler.Connect();
    RootFolder := Scheduler.GetFolder('\');

    TaskDef := Scheduler.NewTask(0);
    TaskDef.RegistrationInfo.Author := 'USBip Installer';
    TaskDef.RegistrationInfo.Description := 'USBip: detach all imported devices on system reboot or shutdown';

    Trigger := TaskDef.Triggers.Create(0); // TASK_TRIGGER_EVENT
    Trigger.Subscription := '<QueryList><Query Id="0" Path="System"><Select Path="System">*[System[Provider[@Name=''Microsoft-Windows-Kernel-Power''] and (EventID=109)]] or *[System[Provider[@Name=''User32''] and (EventID=1074)]]</Select></Query></QueryList>';

    Principal := TaskDef.Principal;
    Principal.UserId := 'S-1-5-19';
    Principal.RunLevel := 0; // TASK_RUNLEVEL_LUA

    Settings := TaskDef.Settings;
    Settings.DisallowStartIfOnBatteries := False;
    Settings.StopIfGoingOnBatteries := False;
    Settings.AllowHardTerminate := True;
    Settings.StartWhenAvailable := False;
    Settings.RunOnlyIfNetworkAvailable := False;
    Settings.ExecutionTimeLimit := 'PT30S';
    Settings.Priority := 7;

    Action := TaskDef.Actions.Create(0); // TASK_ACTION_EXEC
    Action.Path := ExpandConstant('{app}\usbip.exe');
    Action.Arguments := 'detach --all=closeonly';

    RootFolder.RegisterTaskDefinition('{#DetachTaskName}', TaskDef, 6, '', '', 5);
    Log('Detach task registered successfully');
  except
    Log('Failed to register detach task: ' + GetExceptionMessage());
  end;
end;

procedure UnregisterDetachTask();
var
  Scheduler, RootFolder: Variant;
begin
  Log('Unregistering detach task: {#DetachTaskName}');
  try
    Scheduler := CreateOleObject('Schedule.Service');
    Scheduler.Connect();
    RootFolder := Scheduler.GetFolder('\');
    try
      RootFolder.GetTask('{#DetachTaskName}');
    except
      Log('Detach task does not exist, skipping deletion');
      Exit;
    end;
    RootFolder.DeleteTask('{#DetachTaskName}', 0);
    Log('Detach task unregistered successfully');
  except
    Log('Failed to unregister detach task: ' + GetExceptionMessage());
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsComponentSelected('client') then
  begin
    RegisterDetachTask();
  end;
end;

function DeleteOemDriverFromRegistry(const DriverName: String): Boolean;
var
  Names: TArrayOfString;
  I, ResultCode: Integer;
  OemName, Prefix: String;
begin
  Result := False;
  Prefix := Lowercase(DriverName + '.inf');
  if RegGetSubkeyNames(HKEY_LOCAL_MACHINE_64, 'SYSTEM\DriverDatabase\DriverPackages', Names) then
  begin
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      if Pos(Prefix, Lowercase(Names[I])) = 1 then
      begin
        if RegQueryStringValue(HKEY_LOCAL_MACHINE_64, 'SYSTEM\DriverDatabase\DriverPackages\' + Names[I], '', OemName) then
        begin
          OemName := Trim(OemName);
          if OemName <> '' then
          begin
            Log('Deleting OEM driver ' + OemName + ' (' + Names[I] + ')');
            if ExecWithNativeSysDir(
                 ExpandConstant('{sys}\pnputil.exe'),
                 '/delete-driver ' + OemName + ' /uninstall /force',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
            begin
              Log(Format('pnputil /delete-driver %s returned exit code %d', [OemName, ResultCode]));
              Result := True;
            end
            else
            begin
              Log(Format('Failed to execute pnputil for %s, error code %d', [OemName, ResultCode]));
            end;
          end;
        end;
      end;
    end;
  end;
end;

procedure DeleteOemDriverFromFileSearch(const DriverName: String);
var
  FindRec: TFindRec;
  InfDir, CatVal: String;
  ResultCode: Integer;
begin
  InfDir := ExpandConstant('{win}\INF\');
  if FindFirst(InfDir + 'oem*.inf', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
        begin
          CatVal := Lowercase(GetIniString('Version', 'CatalogFile', '', InfDir + FindRec.Name));
          if (Pos(Lowercase(DriverName), CatVal) > 0) or
             (GetIniString('SourceDisksFiles', DriverName + '.sys', '', InfDir + FindRec.Name) <> '') then
          begin
            Log('Deleting OEM driver ' + FindRec.Name + ' (' + DriverName + ')');
            if ExecWithNativeSysDir(
                 ExpandConstant('{sys}\pnputil.exe'),
                 '/delete-driver ' + FindRec.Name + ' /uninstall /force',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
            begin
              Log(Format('pnputil /delete-driver %s returned exit code %d', [FindRec.Name, ResultCode]));
            end
            else
            begin
              Log(Format('Failed to execute pnputil for %s, error code %d', [FindRec.Name, ResultCode]));
            end;
          end;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure DeleteOemDriver(const DriverName: String);
begin
  DeleteOemDriverFromRegistry(DriverName);
  DeleteOemDriverFromFileSearch(DriverName);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    UnregisterDetachTask();
  end
  else if CurUninstallStep = usPostUninstall then
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
  else if FileExists(UninstStr) then
  begin
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

function PrepareToInstall(var NeedsRestart: Boolean): string;
var
  UninstPath, UninstParams: String;
  ExitCode: Integer;
begin
  Result := '';
  if GetInstalledUninstallString(UninstPath, UninstParams) then
  begin
    Log('Uninstalling previous package: ' + UninstPath + ' ' + UninstParams);
    if Exec(UninstPath, UninstParams, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    begin
      if ExitCode = 0 then
        Log('Installed package uninstall completed successfully')
      else
        Result := 'Failed to automatically uninstall the previous version of USBip (exit code ' + IntToStr(ExitCode) + '). ' +
                  'Please uninstall it manually before continuing.';
    end
    else
    begin
      Result := 'Failed to execute uninstaller for the previous version of USBip: ' + SysErrorMessage(ExitCode) + '. ' +
                'Please uninstall it manually before continuing.';
    end;
  end;
end;
