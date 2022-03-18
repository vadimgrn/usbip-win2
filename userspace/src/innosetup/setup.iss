; (C) 2022 Vadym Hrynchyshyn

#ifndef SolutionDir
        #error Use option /DSolutionDir=<path>
#endif

#ifndef ExePath
        #error Use option /DExePath=path-to-exe
#endif

#define BuildDir AddBackslash(ExtractFilePath(ExePath))

#define TestCert "USBIP Test"
#define ENV_PATH "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"

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

[Types]
Name: "full"; Description: "Full"
Name: "client"; Description: "Client"
Name: "server"; Description: "Server"

[Components]
Name: client; Description: "client"; Types: full client; Flags: fixed
Name: server; Description: "server"; Types: full server; Flags: fixed

[Tasks]
Name: path; Description: "Append path to binaries to PATH environment variable for all users";

[Files]
Source: {#BuildDir + "usbip.exe"}; DestDir: "{app}"
Source: {#BuildDir + "usbip_xfer.exe"}; DestDir: "{app}"
Source: {#SolutionDir + "userspace\usb.ids"}; DestDir: "{app}"
Source: {#SolutionDir + "driver\usbip_test.pfx"}; DestDir: "{tmp}"
Source: {#SolutionDir + "Readme.md"}; DestDir: "{app}"; Flags: isreadme

Source: {#BuildDir + "package\usbip_root.inf"}; DestDir: "{tmp}"; Components: client
Source: {#BuildDir + "package\usbip_vhci.inf"}; DestDir: "{tmp}"; Components: client
Source: {#BuildDir + "package\usbip_vhci.sys"}; DestDir: "{tmp}"; Components: client
Source: {#BuildDir + "package\usbip_vhci.cat"}; DestDir: "{tmp}"; Components: client

Source: {#BuildDir + "usbipd.exe"}; DestDir: "{app}"; Components: server
Source: {#BuildDir + "package\usbip_stub.sys"}; DestDir: "{app}"; Components: server
Source: {#SolutionDir + "driver\stub\usbip_stub.inx"}; DestDir: "{app}"; Components: server

[Run]

Filename: {sys}\certutil.exe; Parameters: "-f -p usbip -importPFX Root ""{tmp}\usbip_test.pfx"" FriendlyName=""{#TestCert}"""; Flags: runhidden
Filename: {sys}\certutil.exe; Parameters: "-f -p usbip -importPFX TrustedPublisher ""{tmp}\usbip_test.pfx"" FriendlyName=""{#TestCert}"""; Flags: runhidden

Filename: {sys}\pnputil.exe; Parameters: "/add-driver {tmp}\usbip_vhci.inf /install"; WorkingDir: "{tmp}"; Components: client; Flags: runhidden
Filename: {sys}\pnputil.exe; Parameters: "/add-driver {tmp}\usbip_root.inf /install"; WorkingDir: "{tmp}"; Components: client; Flags: runhidden

; Filename: {sys}\pnputil.exe; Parameters: "/add-driver {tmp}\usbip_stub.inx /install"; WorkingDir: "{tmp}"; Components: server; Flags: runhidden

[UninstallRun]

Filename: {cmd}; Parameters: "/c FOR /F %P IN ('findstr /m ""CatalogFile=usbip_vhci.cat"" {win}\INF\oem*.inf') DO {sys}\pnputil.exe /delete-driver %~nxP /uninstall"; RunOnceId: "RmClientDrivers"; Components: client; Flags: runhidden
; Filename: {cmd}; Parameters: "/c FOR /F %P IN ('findstr /m ""CatalogFile=usbip_stub.cat"" {win}\INF\oem*.inf') DO {sys}\pnputil.exe /delete-driver %~nxP /uninstall"; RunOnceId: "RmServerDrivers"; Components: server; Flags: runhidden

Filename: {sys}\certutil.exe; Parameters: "-f -delstore Root ""{#TestCert}"""; RunOnceId: "RmCertRoot"; Flags: runhidden
Filename: {sys}\certutil.exe; Parameters: "-f -delstore TrustedPublisher ""{#TestCert}"""; RunOnceId: "RmCertTrustedPublisher"; Flags: runhidden
