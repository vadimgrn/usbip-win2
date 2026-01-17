; Copyright (C) 2022 - 2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>

#if Ver < EncodeVer(6,4,2,0)
        #error This script requires Inno Setup 6.4.2 or later
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
#define Copyright GetFileCopyright(ExePath)
#define Company GetFileCompany(ExePath)

#define AppGUID "{199505b0-b93d-4521-a8c7-897818e0205a}"

#define FilterDriver "usbip2_filter"
#define UdeDriver "usbip2_ude"

#define CLIENT_HWID "ROOT\USBIP_WIN2\UDE"

#define TimestampServer "http://timestamp.digicert.com"

; project's test certificate
#define CertFileName "usbip.pfx"
#define CertFilePath SolutionDir + "drivers\package\" + CertFileName
#define CertName "USBip"
#define CertPwd "usbip"

; whether SignTool directive uses the project's test certificate
#define TEST_SIGNED_SETUP

; whether drivers are signed by the project's test certificate
#define TEST_SIGNED_DRIVERS

#define INSTALL_TEST_CERTIFICATE (Defined(TEST_SIGNED_SETUP) || Defined(TEST_SIGNED_DRIVERS))

[Setup]
AppName={#ProductName}
AppVersion={#AppVersion}
AppCopyright={#Copyright}
AppPublisher={#Company}
AppPublisherURL=https://github.com/vadimgrn/usbip-win2
WizardStyle=modern
DefaultDirName={autopf}\{#ProductName}
DefaultGroupName={#ProductName}
ArchitecturesAllowed=x86 or x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
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
SignTool=sign_util sign /f $q{#CertFilePath}$q /p {#CertPwd} /tr {#TimestampServer} /td sha256 /fd sha256 $f

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
Source: {#SolutionDir + "userspace\innosetup\UninsIS.dll"}; Flags: dontcopy signonce; Components: main
Source: {#SolutionDir + "userspace\innosetup\PathMgr.dll"}; DestDir: "{app}"; Flags: uninsneveruninstall signonce; Components: main

Source: {#BuildDir + "usbip.exe"}; DestDir: "{app}"; Flags: signonce; Components: main
Source: {#BuildDir + "devnode.exe"}; DestDir: "{app}"; Flags: signonce; Components: main
Source: {#BuildDir + "*.dll"}; DestDir: "{app}"; Flags: signonce; Components: main

Source: {#SolutionDir + "userspace\libusbip\*.h"}; DestDir: "{app}\include\usbip"; Excludes: "resource.h"; Components: sdk
Source: {#SolutionDir + "userspace\resources\messages.h"}; DestDir: "{app}\include\usbip"; Components: sdk
Source: {#BuildDir + "libusbip.lib"}; DestDir: "{app}\lib"; Components: sdk
Source: {#BuildDir + "libusbip.exp"}; DestDir: "{app}\lib"; Components: sdk

Source: {#BuildDir + "*.pdb"}; DestDir: "{app}"; Excludes: "libusbip*.pdb, wusbip.pdb"; Components: pdb
Source: {#BuildDir + "libusbip.pdb"}; DestDir: "{app}"; Components: pdb or sdk
; Source: {#BuildDir + "wusbip.pdb"}; DestDir: "{app}"; Components: pdb and gui
; wusbip.pdb is too large

Source: {#BuildDir + "wusbip.exe"}; DestDir: "{app}"; Flags: signonce; Components: gui

Source: {#VCToolsRedistInstallDir}{#VCToolsRedistExe}; DestDir: "{tmp}"; Flags: nocompression; Components: main
Source: {#BuildDir + "package\*"}; DestDir: "{tmp}"; Components: main

#if INSTALL_TEST_CERTIFICATE
  Source: {#CertFilePath}; DestDir: "{tmp}"; Components: main
#endif

[Tasks]
Name: vcredist; Description: "Install Microsoft Visual C++ &Redistributable({#Platform})"
Name: modifypath; Description: "Add to &PATH environment variable for all users"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: gui

[Run]

Filename: {tmp}\{#VCToolsRedistExe}; Parameters: "/quiet /norestart"; Tasks: vcredist

#if INSTALL_TEST_CERTIFICATE
  Filename: {sys}\certutil.exe; Parameters: "-f -p ""{#CertPwd}"" -importPFX root ""{tmp}\{#CertFileName}"" FriendlyName=""{#CertName}"""; Flags: runhidden
#endif

Filename: {sys}\pnputil.exe; Parameters: "/add-driver {tmp}\{#FilterDriver}.inf /install"; Flags: runhidden; Components: client
Filename: {app}\devnode.exe; Parameters: "install {tmp}\{#UdeDriver}.inf {#CLIENT_HWID}"; Flags: runhidden; Components: client

[UninstallRun]

Filename: {app}\devnode.exe; Parameters: "remove {#CLIENT_HWID} root"; Flags: runhidden

; FIXME: findstr cannot search Unicode files, /Q:u switch is used to supress warnings
Filename: {cmd}; Parameters: "/c FOR /f %P IN ('findstr /M /L /Q:u {#UdeDriver}    {win}\INF\oem*.inf') DO {sys}\pnputil.exe /delete-driver %~nxP /uninstall"; Flags: runhidden
Filename: {cmd}; Parameters: "/c FOR /f %P IN ('findstr /M /L /Q:u {#FilterDriver} {win}\INF\oem*.inf') DO {sys}\pnputil.exe /delete-driver %~nxP /uninstall"; Flags: runhidden

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
  name := 'Element'
  
  result := RegQueryStringValue(HKEY_LOCAL_MACHINE, subkey, name, value);
  if not result then
    exit;
   
  subkey := make_bcd_subkey_path(value, '16000049'); // AllowPrereleaseSignatures
  result := RegQueryBinaryValue(HKEY_LOCAL_MACHINE, subkey, name, binval) and (binval = #1)
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

procedure InitializeWizard();
begin
  WizardForm.LicenseAcceptedRadio.Checked := True;
end;

function UninstallNeedRestart(): Boolean;
begin
  result := true;
end;


// Inno Setup Third-Party Files, PathMgr.dll
// https://github.com/Bill-Stewart/PathMgr
// The code is copied as is from [Code] section of PathMan.iss,
// except for InitializeSetup function, which was modified.

const
  MODIFY_PATH_TASK_NAME = 'modifypath';  // Specify name of task

var
  PathIsModified: Boolean;          // Cache task selection from previous installs
  ApplicationUninstalled: Boolean;  // Has application been uninstalled?

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
  result := check_test_sign_mode();
  if result then
  begin
    // Was task selected during a previous install?
    PathIsModified := GetPreviousData(MODIFY_PATH_TASK_NAME, '') = 'true';
  end;
end;

function InitializeUninstall(): Boolean;
begin
  result := true;
  // Was task selected during a previous install?
  PathIsModified := GetPreviousData(MODIFY_PATH_TASK_NAME, '') = 'true';
  ApplicationUninstalled := false;
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
  end
  else if CurUninstallStep = usPostUninstall then
  begin
    ApplicationUninstalled := true;
  end;
end;

procedure DeinitializeUninstall();
begin
  if ApplicationUninstalled then
  begin
    // Unload and delete PathMgr.dll and remove app dir when uninstalling
    UnloadDLL(ExpandConstant('{app}\PathMgr.dll'));
    DeleteFile(ExpandConstant('{app}\PathMgr.dll'));
    RemoveDir(ExpandConstant('{app}'));
  end;
end;

// end of PathMgr.dll


// UninsIS.dll
// https://github.com/Bill-Stewart/UninsIS
// The code is copied from [Code] section of UninsIS-Sample.iss, following modifications are made:
// 1) CompareISPackageVersion is removed because it MUST always be uninstalled
// 2) PrepareToInstall does not call it

// Import IsISPackageInstalled() function from UninsIS.dll at setup time
function DLLIsISPackageInstalled(AppId: string; Is64BitInstallMode,
  IsAdminInstallMode: DWORD): DWORD;
  external 'IsISPackageInstalled@files:UninsIS.dll stdcall setuponly';

// Import UninstallISPackage() function from UninsIS.dll at setup time
function DLLUninstallISPackage(AppId: string; Is64BitInstallMode,
  IsAdminInstallMode: DWORD): DWORD;
  external 'UninstallISPackage@files:UninsIS.dll stdcall setuponly';

// Wrapper for UninsIS.dll IsISPackageInstalled() function
// Returns true if package is detected as installed, or false otherwise
function IsISPackageInstalled(): Boolean;
begin
  result := DLLIsISPackageInstalled('{#AppGUID}',  // AppId
    DWORD(Is64BitInstallMode()),                   // Is64BitInstallMode
    DWORD(IsAdminInstallMode())) = 1;              // IsAdminInstallMode
  if result then
    Log('UninsIS.dll - Package detected as installed')
  else
    Log('UninsIS.dll - Package not detected as installed');
end;

// Wrapper for UninsIS.dll UninstallISPackage() function
// Returns 0 for success, non-zero for failure
function UninstallISPackage(): DWORD;
begin
  result := DLLUninstallISPackage('{#AppGUID}',  // AppId
    DWORD(Is64BitInstallMode()),                 // Is64BitInstallMode
    DWORD(IsAdminInstallMode()));                // IsAdminInstallMode
  if result = 0 then
    Log('UninsIS.dll - Installed package uninstall completed successfully')
  else
    Log('UninsIS.dll - installed package uninstall did not complete successfully');
end;


function PrepareToInstall(var NeedsRestart: Boolean): string;
begin
  result := '';
  // If package installed, uninstall it automatically if the version we are
  // installing does not match the installed version; If you want to
  // automatically uninstall only...
  // ...when downgrading: change <> to <
  // ...when upgrading:   change <> to >
  if IsISPackageInstalled() then // and (CompareISPackageVersion() <> 0)
    UninstallISPackage();
end;
