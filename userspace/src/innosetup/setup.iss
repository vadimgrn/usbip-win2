; (C) 2022 Vadym Hrynchyshyn

#if Ver < EncodeVer(6,2,0,0)
        #error This script requires Inno Setup 6.2 or later
#endif

#ifndef SolutionDir
        #error Use option /DSolutionDir=<path>
#endif

#ifndef ExePath
        #error Use option /DExePath=path-to-exe
#endif

#define TestCert "USBIP Test"
#define BuildDir AddBackslash(ExtractFilePath(ExePath))

; information from .exe GetVersionInfo
#define ProductName GetStringFileInfo(ExePath, PRODUCT_NAME)
#define VersionInfo GetVersionNumbersString(ExePath)
#define Copyright GetFileCopyright(ExePath)
#define Company GetFileCompany(ExePath)

[Setup]
AppName={#ProductName}
AppVersion={#VersionInfo}
AppCopyright={#Copyright}
AppPublisher={#Company}
AppPublisherURL=https://github.com/vadimgrn/usbip-win2
WizardStyle=modern
DefaultDirName={autopf}\{#ProductName}
DefaultGroupName={#ProductName}
Compression=lzma2/ultra
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
VersionInfoVersion={#VersionInfo}
ShowLanguageDialog=no
AllowNoIcons=yes
LicenseFile={#SolutionDir + "LICENSE"}
AppId=b26d8e8f-5ed4-40e7-835f-03dfcc57cb45
OutputBaseFilename={#ProductName}-{#VersionInfo}-setup
OutputDir={#BuildDir}
DisableWelcomePage=no

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nWindows Test Signing Mode must be enabled. To enable it execute as Administrator%n%nbcdedit.exe /set testsigning on%n%nand reboot Windows.

[Types]
Name: "full"; Description: "Full"
Name: "client"; Description: "Client"
Name: "server"; Description: "Server"

[Components]
Name: client; Description: "client"; Types: full client; Flags: fixed
Name: server; Description: "server"; Types: full server; Flags: fixed

[Files]
Source: {#BuildDir + "usbip.exe"}; DestDir: "{app}"
Source: {#BuildDir + "usbip_xfer.exe"}; DestDir: "{app}"
Source: {#SolutionDir + "userspace\usb.ids"}; DestDir: "{app}"
Source: {#SolutionDir + "userspace\src\innosetup\PathMgr.dll"};  DestDir: "{app}"; Flags: uninsneveruninstall
Source: {#SolutionDir + "Readme.md"}; DestDir: "{app}"; Flags: isreadme
Source: {#SolutionDir + "driver\usbip_test.pfx"}; DestDir: "{tmp}"

Source: {#BuildDir + "package\usbip_root.inf"}; DestDir: "{tmp}"; Components: client
Source: {#BuildDir + "package\usbip_vhci.inf"}; DestDir: "{tmp}"; Components: client
Source: {#BuildDir + "package\usbip_vhci.sys"}; DestDir: "{tmp}"; Components: client
Source: {#BuildDir + "package\usbip_vhci.cat"}; DestDir: "{tmp}"; Components: client

Source: {#BuildDir + "usbipd.exe"}; DestDir: "{app}"; Components: server
Source: {#BuildDir + "package\usbip_stub.sys"}; DestDir: "{app}"; Components: server
Source: {#SolutionDir + "driver\stub\usbip_stub.inx"}; DestDir: "{app}"; Components: server

[Tasks]
Name: modifypath; Description: "&Add to PATH environment variable for all users"

[Run]

Filename: {sys}\certutil.exe; Parameters: "-f -p usbip -importPFX Root ""{tmp}\usbip_test.pfx"" FriendlyName=""{#TestCert}"""; Flags: runhidden
Filename: {sys}\certutil.exe; Parameters: "-f -p usbip -importPFX TrustedPublisher ""{tmp}\usbip_test.pfx"" FriendlyName=""{#TestCert}"""; Flags: runhidden

Filename: {sys}\pnputil.exe; Parameters: "/add-driver {tmp}\usbip_root.inf /install"; WorkingDir: "{tmp}"; Components: client; Flags: runhidden
Filename: {sys}\pnputil.exe; Parameters: "/add-driver {tmp}\usbip_vhci.inf /install"; WorkingDir: "{tmp}"; Components: client; Flags: runhidden

[UninstallRun]

Filename: {cmd}; Parameters: "/c FOR /F %P IN ('findstr /m ""CatalogFile=usbip_vhci.cat"" {win}\INF\oem*.inf') DO {sys}\pnputil.exe /delete-driver %~nxP /uninstall"; RunOnceId: "DelClientDrivers"; Components: client; Flags: runhidden

Filename: {sys}\certutil.exe; Parameters: "-f -delstore Root ""{#TestCert}"""; RunOnceId: "DelCertRoot"; Flags: runhidden
Filename: {sys}\certutil.exe; Parameters: "-f -delstore TrustedPublisher ""{#TestCert}"""; RunOnceId: "DelCertTrustedPublisher"; Flags: runhidden

; Inno Setup Third-Party Files, PathMgr.dll
; https://github.com/Bill-Stewart/PathMgr
; [Code] section is copied as is from EditPath.iss

[Code]
const
  MODIFY_PATH_TASK_NAME = 'modifypath';  // Specify name of task

var
  PathIsModified: Boolean;  // Cache task selection from previous installs

// Import AddDirToPath() at setup time ('files:' prefix)
function DLLAddDirToPath(DirName: string; PathType, AddType: DWORD): DWORD;
  external 'AddDirToPath@files:PathMgr.dll stdcall setuponly';

// Import RemoveDirFromPath() at uninstall time ('{app}\' prefix)
function DLLRemoveDirFromPath(DirName: string; PathType: DWORD): DWORD;
  external 'RemoveDirFromPath@{app}\PathMgr.dll stdcall uninstallonly';

// Wrapper for AddDirToPath() DLL function
function AddDirToPath(const DirName: string): DWORD;
var
  PathType, AddType: DWORD;
begin
  // PathType = 0 - use system Path
  // PathType = 1 - use user Path
  // AddType = 0 - add to end of Path
  // AddType = 1 - add to beginning of Path
  if IsAdminInstallMode() then
    PathType := 0
  else
    PathType := 1;
  AddType := 0;
  result := DLLAddDirToPath(DirName, PathType, AddType);
end;

// Wrapper for RemoveDirFromPath() DLL function
function RemoveDirFromPath(const DirName: string): DWORD;
var
  PathType: DWORD;
begin
  // PathType = 0 - use system Path
  // PathType = 1 - use user Path
  if IsAdminInstallMode() then
    PathType := 0
  else
    PathType := 1;
  result := DLLRemoveDirFromPath(DirName, PathType);
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  // Store previous or current task selection as custom user setting
  if PathIsModified or WizardIsTaskSelected(MODIFY_PATH_TASK_NAME) then
    SetPreviousData(PreviousDataKey, MODIFY_PATH_TASK_NAME, 'true');
end;

function InitializeSetup(): Boolean;
begin
  result := true;
  // Was task selected during a previous install?
  PathIsModified := GetPreviousData(MODIFY_PATH_TASK_NAME, '') = 'true';
end;

function InitializeUninstall(): Boolean;
begin
  result := true;
  // Was task selected during a previous install?
  PathIsModified := GetPreviousData(MODIFY_PATH_TASK_NAME, '') = 'true';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Add app directory to Path at post-install step if task selected
    if PathIsModified or WizardIsTaskSelected(MODIFY_PATH_TASK_NAME) then
      AddDirToPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    // Remove app directory from path during uninstall if task was selected;
    // use variable because we can't use WizardIsTaskSelected() at uninstall
    if PathIsModified then
      RemoveDirFromPath(ExpandConstant('{app}'));
  end;
end;

procedure DeinitializeUninstall();
begin
  // Unload and delete PathMgr.dll and remove app dir when uninstalling
  UnloadDLL(ExpandConstant('{app}\PathMgr.dll'));
  DeleteFile(ExpandConstant('{app}\PathMgr.dll'));
  RemoveDir(ExpandConstant('{app}'));
end;

// end of PathMgr.dll

procedure InitializeWizard();
begin
  WizardForm.LicenseAcceptedRadio.Checked := True;
end;