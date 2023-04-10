[![latest release](https://img.shields.io/github/v/release/vadimgrn/usbip-win2?include_prereleases)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![release date](https://img.shields.io/github/release-date-pre/vadimgrn/usbip-win2)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![downloads](https://img.shields.io/github/downloads-pre/vadimgrn/usbip-win2/latest/total)](https://github.com/vadimgrn/usbip-win2/releases/latest) [![commits since](https://img.shields.io/github/commits-since/vadimgrn/usbip-win2/latest/develop?include_prereleases "commits since")](https://github.com/vadimgrn/usbip-win2/commits/develop) [![commit activity](https://img.shields.io/github/commit-activity/m/vadimgrn/usbip-win2/develop "commit activity")](https://github.com/vadimgrn/usbip-win2/commits/develop) [![license](https://img.shields.io/github/license/vadimgrn/usbip-win2)](https://github.com/vadimgrn/usbip-win2/blob/master/LICENSE)

# USB/IP Client for Windows
- Fully compatible with [USB/IP protocol](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- Works with Linux USB/IP server at least for kernels 4.19 - 5.19
- **Is not ready for production use**, can cause BSOD
- There is no "official" USB/IP client for Windows
- The driver is not signed, [Windows Test Signing Mode](https://docs.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option) must be enabled
- You can donate to purchase Extended Validation Code Signing Certificate

  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/R5R8JD80B)
  [!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/white_img.png)](https://www.buymeacoffee.com/usbip)

## Two implementations
- [UDE driver](https://github.com/vadimgrn/usbip-win2/tree/master) (version 0.9.5 and later)
  - Is stable, but **has known issues** for some kind of devices (at least audio devices)
  - Should be used if your devices work with it
  - [Devices](https://github.com/vadimgrn/usbip-win2/wiki#ude-driver-list-of-devices-known-to-work) that work (list is incomplete)
- [WDM driver](https://github.com/vadimgrn/usbip-win2/tree/wdm) (versions up to 0.9.5)
  - **Is fully implemented**
  - The latest release is [0.9.3.3](https://github.com/vadimgrn/usbip-win2/releases/tag/v.0.9.3.3)
  - Development stopped in favor of UDE driver
  - Use it only if UDE driver has issues with your devices
  - Will be supported until UDE driver is fully functional
  - [Devices](https://github.com/vadimgrn/usbip-win2/wiki#wdm-driver-list-of-devices-known-to-work) that work (list is incomplete)
- UDE and WDM drivers can be installed and used together on the same PC, just make sure you use the appropriate usbip.exe for each one

## Requirements
- Windows 10 x64 Version [2004](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlisntddiversionavailable) (OS build 19041) and later
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
- Windows 10 v.2004 or later is required
- C++ 20 is used for all projects
- Visual Studio 2022 is used
- Server (stub driver) is removed
- x86 build is removed

## Build

### Notes
- Driver is configured for Windows 10 v.2004 target
- x86 platform is not supported
- Build is tested on the latest Windows 11 Pro

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
```
rem devcon classfilter usb upper !usbip2_filter
"C:\Program Files\USBip\classfilter" remove upper "{36FC9E60-C465-11CF-8056-444553540000}" usbip2_filter
pnputil /remove-device /deviceid ROOT\USBIP_WIN2\UDE /subtree
FOR /f %P IN ('findstr /M /L "Manufacturer=\"USBIP-WIN2\"" C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %~nxP /uninstall
del /Q "C:\Program Files\USBip"
```

## Obtaining USB/IP logs on Windows
- WPP Software Tracing is used
- Use the tools for software tracing, such as TraceView, Tracelog, Tracefmt, and Tracepdb to configure, start, and stop tracing sessions and to display and filter trace messages
- These tools are included in the Windows Driver Kit (WDK)
- Start log sessions for drivers (copy commands to .bat file and run it as Admin)
```
@echo off
set NAME=usbip

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

rm %NAME%-*.*
tracepdb.exe -f "C:\Program Files\USBip\*.pdb" -s -p %TEMP%\%NAME%

tracelog.exe -start %NAME%-flt -guid #90c336ed-69fb-43d6-b800-1552d72d200b -f %NAME%-flt.etl -flag 0x7 -level 5
tracelog.exe -start %NAME%-ude -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%-ude.etl -flag 0xF -level 5
```
- Reproduce the issue
- Stop log sessions and get plain text logs (copy commands to .bat file and run it as Admin)
- If you copy/paste commands directly, use `set TRACE_FORMAT_PREFIX=[%9]%3!04x! %!LEVEL! %!FUNC!:`
```
@echo off
set NAME=usbip
set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-flt.txt %NAME%-flt.etl
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-ude.txt %NAME%-ude.etl

rem sed -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%-*.txt
rem sed -i "s/`anonymous namespace':://" %NAME%-*.txt
rem rm sed*
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
verifier /rc 1 2 4 5 6 9 10 11 12 14 15 16 18 20 24 26 33 34 35 36 /driver usbip2_filter.sys usbip2_ude.sys
```
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
