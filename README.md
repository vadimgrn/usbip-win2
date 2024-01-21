[![latest release](https://img.shields.io/github/v/release/vadimgrn/usbip-win2?include_prereleases)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![release date](https://img.shields.io/github/release-date-pre/vadimgrn/usbip-win2)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![downloads](https://img.shields.io/github/downloads-pre/vadimgrn/usbip-win2/latest/total)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![commits since](https://img.shields.io/github/commits-since/vadimgrn/usbip-win2/latest/develop?include_prereleases "commits since")](https://github.com/vadimgrn/usbip-win2/commits/develop) [![commit activity](https://img.shields.io/github/commit-activity/m/vadimgrn/usbip-win2/develop "commit activity")](https://github.com/vadimgrn/usbip-win2/commits/develop) [![license](https://img.shields.io/github/license/vadimgrn/usbip-win2)](https://github.com/vadimgrn/usbip-win2/blob/master/LICENSE)

# USB/IP Client for Windows
- Fully compatible with [USB/IP protocol](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- Works with Linux USB/IP server at least for kernels 4.19 - 6.2
- **Is not ready for production use**, can cause BSOD
- The driver is not signed, [Windows Test Signing Mode](https://docs.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option) must be enabled
- You can **donate to purchase an EV certificate** which is required for signing the driver, please read [this](https://github.com/vadimgrn/usbip-win2/issues/48#issuecomment-1888655412) thread

## Two implementations
- [UDE driver](https://github.com/vadimgrn/usbip-win2/tree/master) (version 0.9.5 and later)
  - Is stable, but **has known issues** for some kind of devices (at least audio devices)
  - Should be used if your devices work with it
  - [Devices](https://github.com/vadimgrn/usbip-win2/wiki#ude-driver-list-of-devices-known-to-work) that work (list is incomplete)
- [WDM driver](https://github.com/vadimgrn/usbip-win2/tree/wdm) (versions up to 0.9.5)
  - **Is fully implemented**
  - The latest release is [0.9.3.4](https://github.com/vadimgrn/usbip-win2/releases/tag/wdm-0.9.3.4)
  - Development stopped in favor of UDE driver
  - Use it only if UDE driver has issues with your devices
  - Will be supported until UDE driver is fully functional
  - [Devices](https://github.com/vadimgrn/usbip-win2/wiki#wdm-driver-list-of-devices-known-to-work) that work (list is incomplete)
- UDE and WDM drivers can be installed and used together on the same PC, just make sure you use the appropriate usbip.exe for each one

## Requirements
- Windows 10 x64 Version [1809](https://en.wikipedia.org/wiki/Windows_10,_version_1809) (OS build 17763) and later
- Server must support USB/IP protocol v.1.1.1

## Key features
- [UDE](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/developing-windows-drivers-for-emulated-usb-host-controllers-and-devices) driver is an USB/IP client
- USB class upper filter driver usbip2_filter is used as companion for UDE driver
- [Winsock Kernel NPI](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) is used
  - The driver establishes TCP/IP connection with a server and does data exchange
  - This implies low latency and high throughput, absence of frequent CPU context switching and a lot of syscalls
- [Zero copy](https://en.wikipedia.org/wiki/Zero-copy) of transfer buffers is implemented for network send and receive operations
  - [Memory Descriptor List](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/using-mdls) is used to send multiple buffers in a single call ([vectored I/O](https://en.wikipedia.org/wiki/Vectored_I/O))
  - [WskSend](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wsk/nc-wsk-pfn_wsk_send) reads data from URB transfer buffer
  - [WskReceive](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wsk/nc-wsk-pfn_wsk_receive) writes data to URB transfer buffer
- [System Worker Threads](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/system-worker-threads) are used to initiate receive operation

## Differences with [cezanne/usbip-win](https://github.com/cezanne/usbip-win)
- Brand new UDE driver, not inherited from the parent repo
- Userspace code is fully rewritten (libusbip and usbip utility)
- SDK for third party developers (libusbip public API)
- InnoSetup installer is used for installation of drivers and userspace stuff
- Windows 10 version 1809 or later is required
- C++ 20 is used for all projects
- Visual Studio 2022 is used
- Server (stub driver) is removed
- x64 build only

## Build

### Build Tools
- The latest Microsoft [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/) 2022
- SDK for Windows 11, version 22H2 (10.0.22621.0)
- [WDK for Windows 11](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk), version 22H2 (10.0.22621.0)

### Build Visual Studio solution
- Open `usbip_win2.sln`
- Set certificate driver signing for `package` project
  - Right-click on the `Project > Properties > Driver Signing > Test Certificate`
  - Enter `usbip.pfx` (password: usbip)
- Build the solution
- All output files are created under x64/{Debug,Release} folders.

## Setup USB/IP server on Ubuntu Linux
- Install required packages
```
sudo apt install linux-tools-generic linux-cloud-tools-generic
sudo modprobe -a usbip-core usbip-host
sudo usbipd -D
```
- List available USB devices
  - `usbip list -l`
```
 - busid 3-2 (1005:b113)
   Apacer Technology, Inc. : Handy Steno/AH123 / Handy Steno 2.0/HT203 (1005:b113)
 - busid 3-3.2 (07ca:513b)
   AVerMedia Technologies, Inc. : unknown product (07ca:513b)
```
- Bind desired USB device
  - `sudo usbip bind -b 3-2`
```
usbip: info: bind device on busid 3-2: complete
```
- Your device 3-2 now can be used by usbip client

## Setup USB/IP on Windows

### Enable Windows Test Signing Mode
- `bcdedit.exe /set testsigning on`
- Reboot the system to apply

### Install USB/IP
- Download and run an installer from [releases](https://github.com/vadimgrn/usbip-win2/releases)
- Some antivirus programs issue false positives for InnoSetup installers

### Use usbip.exe to attach remote device(s)
- Query available USB devices on the server
  - `usbip.exe list -r <usbip server ip>`
```
Exportable USB devices
======================
 - 192.168.1.9
        3-2: unknown vendor : unknown product (1005:b113)
           : /sys/devices/pci0000:00/0000:00:14.0/usb3/3-2
           : (Defined at Interface level) (00/00/00)
```
- Attach desired remote USB device using its busid
  - `usbip.exe attach -r <usbip server ip> -b 3-2`
```
successfully attached to port 1
```
- New USB device should appear in the system, use it as usual
- Detach the remote USB device using its usb port, pass `-all` to detach all remote devices
  - `usbip.exe detach -p 1`
```
port 1 is successfully detached
```
### Uninstallation of USB/IP
- Uninstall USB/IP app
- Disable test signing
    - `bcdedit.exe /set testsigning off`
  - Reboot the system to apply
- If an uninstaller is corrupted, run these commands as Administrator
- **If you copy commands to a .bat file, use %%P and %%~nxP in FOR statement**
```
set APPDIR=C:\Program Files\USBip
set HWID=ROOT\USBIP_WIN2\UDE

"%APPDIR%\usbip.exe" detach --all

"%APPDIR%\devnode.exe" remove %HWID% root
rem pnputil.exe /remove-device /deviceid %HWID% /subtree

rem WARNING: use %%P and %%~nxP if you run this command in a .bat file
FOR /f %P IN ('findstr /M /L %HWID% C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %~nxP /uninstall

"%APPDIR%\classfilter.exe" uninstall "%APPDIR%\usbip2_filter.inf" DefaultUninstall.NTamd64
rd /S /Q "%APPDIR%"
```
### Disable Windows Test Signing Mode without removing USB/IP
- usbip2_filter driver must be disabled, otherwise all USB controllers/devices will not work
```
devcon.exe classfilter usb upper !usbip2_filter
bcdedit.exe /set testsigning off
```
- To enable it again
```
devcon.exe classfilter usb upper +usbip2_filter
bcdedit.exe /set testsigning on
```
- If devcon.exe is not installed
  - Run regedit.exe
  - Open key HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\\{36fc9e60-c465-11cf-8056-444553540000}
  - Remove ```usbip2_filter``` line from ```UpperFilters``` multi-string value to disable the driver
  - Add this line to enable the driver

## Obtaining USB/IP logs on Windows
- WPP Software Tracing is used
- Use the tools for software tracing, such as TraceView, Tracelog, Tracefmt, and Tracepdb to configure, start, and stop tracing sessions and to display and filter trace messages
- These tools are included in the [Windows Driver Kit](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk#download-icon-step-3-install-windows-11-version-22h2-wdk)
- **Pick "Select Components/Program DataBase files" during USBip installation**
- Start log sessions for drivers (run commands as Administrator)
```
set NAME=usbip

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

del /F %NAME%-*.*
tracepdb.exe -f "C:\Program Files\USBip\*.pdb" -s -p %TEMP%\%NAME%

tracelog.exe -start %NAME%-flt -guid #90c336ed-69fb-43d6-b800-1552d72d200b -f %NAME%-flt.etl -flag 0x3 -level 5
tracelog.exe -start %NAME%-ude -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%-ude.etl -flag 0xF -level 5
```
- Reproduce the issue
- Stop log sessions and get plain text logs (run commands as Administrator)
- **If you copy commands to a .bat file, double '%' in TRACE_FORMAT_PREFIX**
```
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

## Debugging [BSOD](https://en.wikipedia.org/wiki/Blue_screen_of_death)
- Enable kernel memory dump
  - Open "System Properties" dialog box
  - Select "Advanced" tab
  - Click on "Settings" in "Startup and Recovery"
  - "System failure", "Write debugging information", pick "Automatic Memory Dump" or "Kernel Memory Dump"
  - Check "Overwrite any existing file"
- Start WPP tracing session for drivers as described in the previous topic
- When BSOD has occured
  - Reboot PC if automatic reboot is not set
  - Run Windows debugger WinDbg.exe as Administrator
  - Press Ctrl+D to open crash dump in C:\Windows
  - Run following commands and copy the output
```
!analyze -v
!wdfkd.wdfsearchpath %TEMP%\USBip
!wdfkd.wdfsettraceprefix [%9]%3!04x! %!LEVEL! %!FUNC!:
!wdfkd.wdflogdump usbip2_ude -d
!wdfkd.wdflogdump usbip2_ude -f
```

## Obtaining USB/IP log on Linux
```
sudo killall usbipd
sudo modprobe -r usbip-host usbip-vudc vhci-hcd usbip-core
sudo modprobe usbip-core usbip_debug_flag=0xFFFFFFFF
sudo modprobe -a usbip-host usbip-vudc vhci-hcd
sudo usbipd -D
dmesg --follow | tee ~/usbip.log
```

## Testing the driver
- [Driver Verifier](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier) is used for testing
- Run verifier.exe as Administrator
- Enable testing
```
verifier /rc 1 2 4 5 6 8 9 12 18 34  10 11 14 15 16 17 20 24 26 33 35 36 /driver usbip2_filter.sys usbip2_ude.sys
```
- Be aware that rule class 26 "Code integrity checking" forces to use NonPagedPoolNx instead of NonPagedPool
- Query driver statistics
```
verifier /query
```
- Disable testing
```
verifier /reset
```
- To run Static Driver Verifier, set "Treat Warnings As Errors" to "No" for libdrv, usbip2_filter, usbip2_ude projects

### If you like this project
<a href="https://www.buymeacoffee.com/usbip" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-blue.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>
