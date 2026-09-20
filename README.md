<p align="center">
  <img src="userspace/wusbip/resources/USBip.svg" width="256" alt="USBip logo"/>
</p>

[![latest release](https://img.shields.io/github/v/release/vadimgrn/usbip-win2?include_prereleases)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![release date](https://img.shields.io/github/release-date-pre/vadimgrn/usbip-win2)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![downloads](https://img.shields.io/github/downloads-pre/vadimgrn/usbip-win2/latest/total)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![commits since](https://img.shields.io/github/commits-since/vadimgrn/usbip-win2/latest/develop?include_prereleases "commits since")](https://github.com/vadimgrn/usbip-win2/commits/develop) [![commit activity](https://img.shields.io/github/commit-activity/m/vadimgrn/usbip-win2/develop "commit activity")](https://github.com/vadimgrn/usbip-win2/commits/develop) [![license](https://img.shields.io/github/license/vadimgrn/usbip-win2)](https://github.com/vadimgrn/usbip-win2/blob/master/LICENSE.txt)

# USB/IP Client for Windows
- Fully compatible with the [USB/IP protocol](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- Works with Linux USB/IP servers for kernels 4.19 through 7.0
- **[WHLK](https://en.wikipedia.org/wiki/Windows_Hardware_Lab_Kit) certified (or attestation signed) drivers**
  - Driver signing is made possible by the [Open Source Codesigning Initiative](https://github.com/OSSign)
- **Create a [restore point](https://github.com/vadimgrn/usbip-win2/tree/master?tab=readme-ov-file#install-usbip)** before installing USBip
- [Devices known to work](https://github.com/vadimgrn/usbip-win2/wiki#ude-driver-list-of-devices-known-to-work) (the list is incomplete)

## Requirements
- Windows 10 x64 version [1903](https://en.wikipedia.org/wiki/Windows_10,_version_1903) (OS build 18362) or later
- Windows 11 ARM64
- The USB/IP server must support protocol version 1.1.1

## Key features
- The [UDE](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-emulated-usb-host-controllers-and-devices) driver acts as a USB/IP client
- A device-specific upper filter driver, `usbip2_filter`, is used as a companion for the UDE driver
- [Winsock Kernel NPI](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) is used
  - The driver establishes a TCP/IP connection with the server and exchanges data
- [Zero copy](https://en.wikipedia.org/wiki/Zero-copy) of transfer buffers is implemented for network send and receive operations
  - A [Memory Descriptor List](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/using-mdls) (MDL) is used to send multiple buffers in a single call ([vectored I/O](https://en.wikipedia.org/wiki/Vectored_I/O))
  - [WskSend](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wsk/nc-wsk-pfn_wsk_send) reads data directly from the URB transfer buffer
  - [WskReceive](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wsk/nc-wsk-pfn_wsk_receive) writes data directly to the URB transfer buffer
- Two implementations for receiving data from the network:
  - Zero Copy:
    - This is the default method
    - A dedicated thread is created for each virtual device to execute a loop with two blocking calls:
      - Receive USB/IP header
      - Receive USB/IP payload (if any)
    - Data is written directly to `URB.TransferBuffer` without any additional copying
  - Low Latency:
    - [WSK event callback functions](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/using-winsock-kernel-functions-vs--event-callback-functions) are used
    - No dedicated receive thread required (no CPU context switching)
    - Data is first written to an intermediate ring buffer, then copied to `URB.TransferBuffer`

## Build

### Build Tools
- [Visual Studio 2026](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk#download-icon-for-visual-studio-step-1-install-visual-studio-2026)
- SDK/WDK installation is not required; [NuGet](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget#how-to-install-wdk-nuget) installs them automatically

### Build Visual Studio solution
- Install required components:
  - Run the Visual Studio Installer
  - Select the "Individual components" tab, find and check:
    - Git for Windows
    - Windows Driver Kit
    - C++ Clang Compiler for Windows (optional, if using the LLVM/ClangCL toolset)
  - Install the selected items
- Clone the repository using git or download and extract the source archive from GitHub
- Run `bootstrap.bat`
- Open `usbip_win2.slnx`
- Configure driver test signing for the `package` project:
  - Right-click `Project > Properties > Driver Signing > Test Certificate`
  - Enter `usbip.pfx` (password: `usbip`)
- Build the solution
- All output files are created under the `{x64,ARM64}/{Debug,Release}` folders.

### Platform Toolset Selection
- Drivers strictly use `WindowsKernelModeDriver10.0` (LLVM is not supported for drivers).
- Userspace projects use MSVC by default (resolving dynamically to the latest MSVC toolset, e.g. `v145` for Visual Studio 2026).
- To select the userspace toolset:
  - In `Directory.Build.props`: change `<UserspacePlatformToolset>` from `msvc` to `ClangCL`:
    ```xml
    <UserspacePlatformToolset Condition="'$(UserspacePlatformToolset)' == ''">ClangCL</UserspacePlatformToolset>
    ```
  - Via command line: pass `/p:UserspacePlatformToolset=...` to `msbuild`:
    ```cmd
    # Build with default MSVC
    msbuild usbip_win2.slnx /p:Configuration=Release /p:Platform=x64

    # Build with LLVM (ClangCL)
    msbuild usbip_win2.slnx /p:Configuration=Release /p:Platform=x64 /p:UserspacePlatformToolset=ClangCL
    ```

## Set up USB/IP server on Ubuntu Linux
- Install required packages:
  - Linux: `sudo apt install linux-tools-generic linux-cloud-tools-generic`
  - Raspberry Pi: `sudo apt install usbip hwdata usbutils`
- Load modules and run the daemon:
```
sudo modprobe -a usbip-core usbip-host
sudo usbipd -D
```
- List available USB devices:
  - `usbip list -l`
```
 - busid 3-2 (1005:b113)
   Apacer Technology, Inc. : Handy Steno/AH123 / Handy Steno 2.0/HT203 (1005:b113)
 - busid 3-3.2 (07ca:513b)
   AVerMedia Technologies, Inc. : unknown product (07ca:513b)
```
- Bind the desired USB device:
  - `sudo usbip bind -b 3-2`
```
usbip: info: bind device on busid 3-2: complete
```
- Device 3-2 can now be used by the USB/IP client.

## Set up USB/IP on Windows

### Enable Windows Test Signing Mode if drivers are not signed by Microsoft
- `bcdedit.exe /set testsigning on`
- Reboot the system to apply.
- **Do not disable test signing while USBip test-signed drivers are installed**, otherwise all connected USB devices will stop working.

### Install USB/IP
- **Create a [restore point](https://support.microsoft.com/en-us/windows/create-a-system-restore-point-77e02e2a-3298-c869-9974-ef5658ea3be9)** to undo possible system crashes:
  - In the search box on the taskbar, type 'Create a restore point', and select it from the list of results.
  - On the System Protection tab in System Properties:
    - Make sure system drive protection is enabled.
    - Select **Create**.
    - Type a description for the restore point, and then select **Create**.
- Download the installer from [Releases](https://github.com/vadimgrn/usbip-win2/releases).
- **All USB 3.0 Hub devices will be restarted during installation:**
  - This means all connected USB devices will stop working for a short time and then resume working.
  - Ensure you do not interrupt critical workflows, such as active file transfers, video calls, or audio recordings.

### Use usbip.exe to attach remote device(s)
- Query available USB devices on the server:
  - `usbip.exe list -r <usbip server ip>`
```
Exportable USB devices
======================
 - 192.168.1.9
        3-2: unknown vendor : unknown product (1005:b113)
           : /sys/devices/pci0000:00/0000:00:14.0/usb3/3-2
           : (Defined at Interface level) (00/00/00)
```
- Attach the desired remote USB device using its busid:
  - `usbip.exe attach -r <usbip server ip> -b 3-2`
```
successfully attached to port 1
```
- The new USB device should now appear in the system; use it as usual.
- Detach the remote USB device using its port number (or pass `-all` to detach all remote devices):
  - `usbip.exe detach -p 1`
```
port 1 is successfully detached
```

### Uninstalling USB/IP
- Uninstall the USB/IP application via Windows Settings > Installed apps.
- Disable test signing if it was enabled during installation:
  - `bcdedit.exe /set testsigning off`
  - Reboot the system to apply.
- If the uninstaller is corrupted, run these commands from an elevated command prompt (Run as Administrator):
- **Note: If copying commands into a `.bat` file, use `%%P` and `%%~nxP` in the `FOR` statement.**
```cmd
SET HWID=ROOT\USBIP_WIN2\UDE
set APPDIR=C:\Program Files\USBip

"%APPDIR%\devnode.exe" remove %HWID% root
rem alternative command since Windows 11, version 21H2
rem pnputil.exe /remove-device /deviceid %HWID% /subtree

rem WARNING: use %%P and %%~nxP if you run this command in a .bat file
FOR /f %P IN ('findstr /M /L /Q:u "usbip2_filter usbip2_ude" C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %~nxP /uninstall

rd /S /Q "%APPDIR%"
```

## Obtaining USB/IP logs on Windows
- WPP Software Tracing is used.
- Use WDK tracing tools, such as TraceView, Tracelog, Tracefmt, and Tracepdb, to configure, start, and stop tracing sessions, and to display and filter trace messages.
- These tools are included in the [Windows Driver Kit](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk#download-icon-step-3-install-windows-11-version-22h2-wdk).
- **Select "Program DataBase files" under "Select Components" during USBip installation.**
- Enable verbose logging:
  - Run as Administrator: `%WINDIR%\system32\reg.exe add HKLM\SYSTEM\CurrentControlSet\Services\usbip2_ude\Parameters\Wdf /v VerboseOn /t REG_DWORD /d 1 /f`
  - Reboot the system.
  - When finished collecting logs, run: `%WINDIR%\system32\reg.exe delete HKLM\SYSTEM\CurrentControlSet\Services\usbip2_ude\Parameters\Wdf /v VerboseOn /f`
- Start trace sessions for drivers (run commands as Administrator):
```cmd
rem change to your WDK version
set PATH=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64

set NAME=usbip

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

del /F %NAME%-*.*
tracepdb.exe -f "C:\Program Files\USBip\*.pdb" -s -p %TEMP%\%NAME%

tracelog.exe -start %NAME%-flt -guid #90c336ed-69fb-43d6-b800-1552d72d200b -f %NAME%-flt.etl -flag 0x3 -level 5
tracelog.exe -start %NAME%-ude -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%-ude.etl -flag 0xF -level 5
```
- Reproduce the issue.
- Stop trace sessions and generate plain text logs (run commands as Administrator):
- **Note: If copying commands into a `.bat` file, double the '%' signs in `TRACE_FORMAT_PREFIX` (`%%%%`).**
```cmd
rem change to your WDK version
set PATH=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64

set NAME=usbip
set TRACE_FORMAT_PREFIX=[%9]%3!04x! %!LEVEL! %!FUNC!:

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-flt.txt %NAME%-flt.etl
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-ude.txt %NAME%-ude.etl

rem sed -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%-*.txt
rem sed -i "s/`anonymous namespace':://" %NAME%-*.txt
rem del /F sed*
```

## Debugging a [BSOD](https://en.wikipedia.org/wiki/Blue_screen_of_death)
- Enable kernel memory dumps:
  - Open the "System Properties" dialog.
  - Select the "Advanced" tab.
  - Under "Startup and Recovery", click "Settings".
  - Under "Write debugging information", select "Automatic Memory Dump" or "Kernel Memory Dump".
  - Check "Overwrite any existing file".
- Start a WPP tracing session for drivers as described in the previous section.
- When a BSOD has occurred:
  - Reboot the PC if automatic restart is disabled.
  - Launch WinDbg (`WinDbg.exe`) as Administrator.
  - Press Ctrl+D to open the crash dump in `C:\Windows\MEMORY.DMP`.
  - Run the following commands and copy the output:
```
.sympath+ C:\Program Files\USBip
!wmitrace.searchpath +%TEMP%\usbip
!analyze -v
!wdfkd.wdfsearchpath %TEMP%\usbip
!wdfkd.wdfsettraceprefix [%9]%3!04x! %!LEVEL! %!FUNC!:
!wdfkd.wdflogdump usbip2_ude -d
!wdfkd.wdflogdump usbip2_ude -f
```

## Obtaining USB/IP logs on Linux
```
sudo killall usbipd
sudo modprobe -r usbip-host usbip-vudc vhci-hcd usbip-core
sudo modprobe usbip-core usbip_debug_flag=0xFFFFFFFF
sudo modprobe -a usbip-host usbip-vudc vhci-hcd
sudo usbipd -D
dmesg --follow | tee ~/usbip.log
```

## Testing the driver
- [Driver Verifier](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier) is used for stress and compliance testing.
- Run `verifier.exe` as Administrator.
- Enable testing:
```
verifier /rc 1 2 4 5 6 8 9 12 18 34  10 11 14 15 16 17 20 24 26 33 35 36 /driver usbip2_filter.sys usbip2_ude.sys
```
- Note: Rule class 26 ("Code integrity checking") enforces the use of `NonPagedPoolNx` instead of `NonPagedPool`.
- Query driver statistics:
```
verifier /query
```
- Disable testing:
```
verifier /reset
```
- To run Static Driver Verifier (SDV), set "Treat Warnings As Errors" to "No" for the `libdrv`, `usbip2_filter`, and `usbip2_ude` projects.

### If you like this project
[![paypal](whlk/jpg/paypal_donate_qrcode.png)](https://www.paypal.com/donate/?hosted_button_id=YNZ9GUFWFYHFW)

[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donate_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=YNZ9GUFWFYHFW)
