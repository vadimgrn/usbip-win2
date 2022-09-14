![license](https://img.shields.io/github/license/vadimgrn/usbip-win2 "License")

# USB/IP Client for Windows
- **Is fully implemented**
- Fully compatible with [USB/IP protocol](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- Works with Linux USB/IP server at least for kernels 4.19 - 5.15
- **Is not ready for production use**, can cause BSOD
- The driver is not signed, [Windows Test Signing Mode](https://docs.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option) must be enabled
- There is no "official" USB/IP client for Windows
- [Devices](https://github.com/vadimgrn/usbip-win2/wiki#list-of-devices-known-to-work) that work (list is incomplete)

## Requirements
- Windows 10 x64, version [2004](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlisntddiversionavailable) and later
- Server must support USB/IP protocol v.1.1.1

## Key features
- WDM driver is a USB/IP client
- [Cancel-Safe IRP Queue](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/cancel-safe-irp-queues) is used
- [Winsock Kernel NPI](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) is used
  - The driver establishes TCP/IP connection with a server and does data exchange
  - This implies low latency and high throughput, absence of frequent CPU context switching and a lot of syscalls
- [Zero copy](https://en.wikipedia.org/wiki/Zero-copy) of transfer buffers is implemented for network send and receive operations
  - [Memory Descriptor Lists](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/using-mdls) are used to send/receive multiple buffers in a single call ([vectored I/O](https://en.wikipedia.org/wiki/Vectored_I/O))
  - [WskSend](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wsk/nc-wsk-pfn_wsk_send) reads data from URB transfer buffer
  - [WskReceive](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wsk/nc-wsk-pfn_wsk_receive) writes data to URB transfer buffer
- [System Worker Threads](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/system-worker-threads) are used to initiate receive operation
  
## Differences with [cezanne/usbip-win](https://github.com/cezanne/usbip-win)
- x86 build is removed
- Server (stub driver) is removed
- UDE client driver is removed, WDM driver is superseded it
- Client (UDE driver)
  - Significantly refactored and improved
  - The core of the driver was rewritten from scratch
  - attacher.exe (usbip_xfer.exe) is no longer used
- C++ 20 is used for all projects
- Visual Studio 2022 is used
- InnoSetup installer is used for installation of the driver and userspace stuff

## Build

### Notes
- Driver is configured for Windows 10 v.2004 target
- x86 platform is not supported
- Build is tested on the latest Windows 11 Pro

### Build Tools
- The latest Microsoft [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/) 2022
- SDK for Windows 11, version 22H2 (10.0.22621.0)
- [WDK for Windows 11](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk), version 22H2 (10.0.22621.0)
- InnoSetup 6.2 or later

### Build Visual Studio solution
- Open `usbip_win.sln`
- Set certificate driver signing for `package` project
  - Right-click on the `Project > Properties > Driver Signing > Test Certificate`
  - Enter `..\usbip_test.pfx` (password: usbip)
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
- Detach the remote USB device using its usb port, pass -1 to detach all remote devices
  - `usbip.exe detach -p 1`
```
port 1 is successfully detached
```
### Uninstallation of USB/IP
- Uninstall USB/IP app
- Disable test signing
    - `bcdedit.exe /set testsigning off`
  - Reboot the system to apply

## Obtaining USB/IP logs on Windows
- WPP Software Tracing is used
- Use the tools for software tracing, such as TraceView, Tracelog, Tracefmt, and Tracepdb to configure, start, and stop tracing sessions and to display and filter trace messages
- These tools are included in the Windows Driver Kit (WDK)
- Use this tracing GUID for UDE driver
  - `ed18c9c5-8322-48ae-bf78-d01d898a1562`
- Install Debug build
- Start a log session for UDE driver (copy commands to .bat file and run it)
```
@echo off
set NAME=usbip2-vhub
tracelog.exe -stop %NAME%
tracelog.exe -start %NAME% -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%.etl -flag 0x1F -level 5
```
- Reproduce the issue
- Stop the log session and get plain text log (copy commands to .bat file and run it)
```
@echo off
set NAME=usbip2-vhub
set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:
tracelog.exe -stop %NAME%
tracepdb.exe -f "C:\Program Files\usbip-win2\*.pdb" -s -p %TEMP%\%NAME%
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%.txt %NAME%.etl
rem sed -i 's/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/' %NAME%.txt
rem rm sed*
```

## Debugging [BSOD](https://en.wikipedia.org/wiki/Blue_screen_of_death)
- Enable kernel memory dump
  - Open "System Properties" dialog box
  - Select "Advanced" tab
  - Click on "Settings" in "Startup and Recovery"
  - "System failure", "Write debugging information", pick "Automatic Memory Dump" or "Kernel Memory Dump"
  - Check "Overwrite any existing file"
- Start WPP tracing session for UDE driver as described in the previous topic
- When BSOD has occured
  - Reboot PC if automatic reboot is not set
  - Run Windows debugger WinDbg.exe as Administrator
  - Press Ctrl+D to open crash dump in C:\Windows
  - Run following commands and copy the output
```
!analyze -v
!wmitrace.searchpath %TEMP%\usbip2-vhub
!wmitrace.setprefix [%9]%3!04x! %!LEVEL! %!FUNC!:
!wmitrace.logdump usbip2-vhub
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
verifier /rc 1 2 4 5 6 9 11 12 16 18 10 14 15 20 24 26 33 34 35 36 /driver usbip2_vhub.sys
```
- Query driver statistics
```
verifier /query
```
- Disable testing
```
verifier /reset
```
- To run Static Driver Verifier, set "Treat Warnings As Errors" to "No" for libdrv and usbip2_vhub projects
