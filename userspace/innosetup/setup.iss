; Copyright (C) 2022 - 2024 Vadym Hrynchyshyn <vadimgrn@gmail.com>

#if Ver < EncodeVer(6,2,0,0)
        #error This script requires Inno Setup 6.2 or later
#endif

#ifndef SolutionDir
        #error Use option /DSolutionDir=<path>
#endif

#ifndef Configuration
        #error Use option /DConfiguration=<cfg>
#endif

#ifndef CpuArch
        #error Use option /DCpuArch=<ARCH>
#endif

#ifdef ExePath
        #define BuildDir AddBackslash(ExtractFilePath(ExePath))
#else
        #error Use option /DExePath=path-to-exe
#endif

#ifndef VCToolsRedistInstallDir
        #error Use option /DVCToolsRedistInstallDir
#endif

#define VCToolsRedistExe "vc_redist.x64.exe"

#define AppExeName ExtractFileName(ExePath)
; #define OutputDir  ExtractFilePath(AppExePath)

; information from .exe GetVersionInfo
#define ProductName GetStringFileInfo(ExePath, PRODUCT_NAME)
#define AppVersion GetVersionNumbersString(ExePath)
#define Copyright GetFileCopyright(ExePath)
#define Company GetFileCompany(ExePath)

#define AppGUID "{199505b0-b93d-4521-a8c7-897818e0205a}"

#define FilterDriver "usbip2_filter"
#define UdeDriver "usbip2_ude"

#define HWID "ROOT\USBIP_WIN2\UDE"

#define CertFile "usbip.pfx"
#define CertName "USBip"
#define CertPwd "usbip"

#define wxWidgetsDLL "wxmsw32u*_vc"

[Setup]
AppName={#ProductName}
AppVersion={#AppVersion}
AppCopyright={#Copyright}
AppPublisher={#Company}
AppPublisherURL=https://github.com/vadimgrn/usbip-win2
WizardStyle=modern
DefaultDirName={autopf}\{#ProductName}
DefaultGroupName={#ProductName}
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
VersionInfoVersion={#AppVersion}
ShowLanguageDialog=no
AllowNoIcons=yes
LicenseFile={#SolutionDir + "LICENSE"}
AppId={{#AppGUID}
OutputBaseFilename={#ProductName}-{#AppVersion}-{#Configuration}
OutputDir={#BuildDir}
SolidCompression=yes
DisableWelcomePage=no
WizardSmallImageFile=48.bmp,64.bmp,128.bmp
WizardImageFile=164.bmp,192.bmp,256.bmp,384.bmp,512.bmp
WizardImageAlphaFormat=defined
WizardImageStretch=no
UninstallDisplayIcon={app}\wusbip.exe

; this app can't be installed more than once
MissingRunOnceIdsWarning=no 

; Windows 10, version 1809
MinVersion=10.0.17763

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%nWindows Test Signing Mode must be enabled. To enable it execute as Administrator%n%nbcdedit.exe /set testsigning on%n%nand reboot Windows.

[Components]
Name: "main"; Description: "Main Files"; Types: full compact custom; Flags: fixed
Name: "gui"; Description: "GUI App"; Types: full custom;
Name: "sdk"; Description: "USBIP Software Development Kit"; Types: full custom
Name: "pdb"; Description: "Program DataBase files"; Types: full custom

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\{#AppExeName}"; Components: gui
Name: "{group}\{cm:UninstallProgram,{#ProductName}}"; Filename: "{uninstallexe}"; Components: main
Name: "{commondesktop}\{#ProductName}"; Filename: "{app}\{#AppExeName}"; Components: gui; Tasks: desktopicon

[Files]

Source: {#SolutionDir + "userspace\innosetup\UninsIS.dll"}; Flags: dontcopy; Components: main

Source: {#SolutionDir + "Readme.md"}; DestDir: "{app}"; Flags: isreadme; Components: main
Source: {#SolutionDir + "userspace\innosetup\PathMgr.dll"}; DestDir: "{app}"; Flags: uninsneveruninstall; Components: main
Source: {#BuildDir + "package\"}{#FilterDriver + ".inf"}; DestDir: "{app}"; Components: main
Source: {#BuildDir + "usbip.exe"}; DestDir: "{app}"; Components: main
Source: {#BuildDir + "devnode.exe"}; DestDir: "{app}"; Components: main
Source: {#BuildDir + "*.dll"}; DestDir: "{app}"; Excludes: "{#wxWidgetsDLL}.dll"; Components: main

Source: {#SolutionDir + "userspace\libusbip\*.h"}; DestDir: "{app}\include\usbip"; Excludes: "resource.h"; Components: sdk
Source: {#SolutionDir + "userspace\resources\messages.h"}; DestDir: "{app}\include\usbip"; Components: sdk
Source: {#BuildDir + "libusbip.lib"}; DestDir: "{app}\lib"; Components: sdk
Source: {#BuildDir + "libusbip.exp"}; DestDir: "{app}\lib"; Components: sdk

; wxmsw32u?_vc.pdb is too large
Source: {#BuildDir + "*.pdb"}; DestDir: "{app}"; Excludes: "libusbip*.pdb, wusbip.pdb, {#wxWidgetsDLL}.pdb"; Components: pdb
Source: {#BuildDir + "libusbip.pdb"}; DestDir: "{app}"; Components: pdb or sdk
Source: {#BuildDir + "wusbip.pdb"}; DestDir: "{app}"; Components: pdb and gui

Source: {#BuildDir + "wusbip.exe"}; DestDir: "{app}"; Components: gui
Source: {#BuildDir}{#wxWidgetsDLL + ".dll"}; DestDir: "{app}"; Components: gui

Source: {#VCToolsRedistInstallDir}{#VCToolsRedistExe}; DestDir: "{tmp}"; Flags: nocompression; Components: main
Source: {#SolutionDir + "drivers\package\"}{#CertFile}; DestDir: "{tmp}"; Components: main
Source: {#BuildDir + "package\*"}; DestDir: "{tmp}"; Components: main

[Tasks]
Name: vcredist; Description: "Install Microsoft Visual C++ &Redistributable(x64)"
Name: modifypath; Description: "Add to &PATH environment variable for all users"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Run]

Filename: {tmp}\{#VCToolsRedistExe}; Parameters: "/quiet /norestart"; Tasks: vcredist 
Filename: {sys}\certutil.exe; Parameters: "-f -p ""{#CertPwd}"" -importPFX root ""{tmp}\{#CertFile}"" FriendlyName=""{#CertName}"""; Flags: runhidden

Filename: {cmd}; Parameters: "/c mklink classfilter.exe devnode.exe"; WorkingDir: "{app}"; Flags: runhidden
Filename: {app}\classfilter.exe; Parameters: "install {tmp}\{#FilterDriver}.inf DefaultInstall.NT{#CpuArch}"; Flags: runhidden

Filename: {app}\devnode.exe; Parameters: "install {tmp}\{#UdeDriver}.inf {#HWID}"; Flags: runhidden

[UninstallRun]

Filename: {app}\devnode.exe; Parameters: "remove {#HWID} root"; Flags: runhidden

; FIXME: usbip2_ude service is not deleted on Win10 version 1809
Filename: {cmd}; Parameters: "/c FOR /f %P IN ('findstr /M /L {#HWID} {win}\INF\oem*.inf') DO {sys}\pnputil.exe /delete-driver %~nxP /uninstall"; Flags: runhidden

Filename: {app}\classfilter.exe; Parameters: "uninstall .\{#FilterDriver}.inf DefaultUninstall.NT{#CpuArch}"; Flags: runhidden
Filename: {cmd}; Parameters: "/c del /F ""{app}\classfilter.exe"""; Flags: runhidden

Filename: {sys}\certutil.exe; Parameters: "-f -delstore root ""{#CertName}"""; Flags: runhidden

[Code]

procedure InitializeWizard();
begin
  WizardForm.LicenseAcceptedRadio.Checked := True;
end;


// Inno Setup Third-Party Files, PathMgr.dll
// https://github.com/Bill-Stewart/PathMgr
// Code is copied as is from [Code] section of EditPath.iss

const
  MODIFY_PATH_TASK_NAME = 'modifypath';  // Specify name of task

var
  PathIsModified: Boolean;  // Cache task selection from previous installs
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
  result := true;
  // Was task selected during a previous install?
  PathIsModified := GetPreviousData(MODIFY_PATH_TASK_NAME, '') = 'true';
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
// Code is copied from [Code] section of UninsIS-Sample.iss, following modifications are made:
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
