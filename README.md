![license](https://img.shields.io/github/license/vadimgrn/usbip-win2 "License")

# USB/IP Client for Windows

- This is USB/IP Client for Windows which is fully compatible with [USB/IP protocol](https://www.kernel.org/doc/html/latest/usb/usbip_protocol.html)
- There is no official USB/IP client for Windows so far

## Status
- Client (VHCI driver)
  - **Is fully implemented**
  - Fully compatible with Linux USBIP server (at least for kernels 4.19 - 5.13)
  - **Is not ready for production use**, can cause BSOD or hang in the kernel. This usually happens during disconnection of a device or uninstallation of the driver.
  - There is no installer
  - The driver is not signed
  - Works on Windows 11/10 x64, x86 build is not supported
- UDE client driver from the parent repo is removed. VHCI driver is superseded it.
- Server (stub driver) is not the goal of this project. It is updated from original repo AS IS.

## Devices that work (list is incomplete)
  - USB 2.0/3.X flash drives
  - Webcams
    - AVerMedia PW513
    - Builtin cam of Dell Alienware 17R3
    - Guillemot Deluxe Optical Glass
  - Headsets
    - Microsoft LifeChat LX-6000
    - Sennheiser ADAPT 160T USB-C II
  - Audio devices
    - C-Media Electronics CM102-A+/102S+ Audio Controller
    - UGREEN External Stereo Sound Adapter (ALC4040)
  - Peripherals
    - Microsoft Wired Keyboard 600 (model 1576)
    - Primax Electronics 0Y357C PMX-MMOCZUL [Dell Laser Mouse]

## Build

### Notes
- Build is tested on Windows 11 x64, projects are configured for Win10 target by default
- x86 platform is not supported

### Build Tools
- The latest Microsoft Visual Studio Community 2019
- Windows 11 SDK (10.1.22000.194)
- Windows Driver Kit (10.1.22000.1)
- vcpkg

### Install Boost C++
- Install [vcpkg](https://vcpkg.io/en/getting-started.html)
  - git clone https://github.com/Microsoft/vcpkg.git
  - .\vcpkg\bootstrap-vcpkg.bat
- Integrate vcpkg with Visual Studio
  - vcpkg integrate install
- Install Boost C++ libraries
  - vcpkg install boost:x64-windows

### Build Visual Studio solution
- Open `usbip_win.sln`
- Set certificate driver signing for `usbip_stub` and `usbip_vhci` projects
  - Right-click on the `Project > Properties > Driver Signing > Test Certificate`
  - Browse to `driver/usbip_test.pfx` (password: usbip)
- Build the solution
- All output files are created under x64/{Debug,Release} folders.

## Setup USBIP server on Ubuntu Linux
- Install required packages
```
apt install linux-tools-generic linux-cloud-tools-generic
modprobe -a usbip-core usbip-host
usbipd -D
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
  - `usbip bind -b 3-2`
```
usbip: info: bind device on busid 3-2: complete
```
- Your device 3-2 now can be used by usbip client

## Setup USBIP client on Windows

### Install USB/IP test certificate
  - Right click on `driver/usbip_test.pfx`, select "Install PFX" (password: usbip)
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) *and*
    2. "Trusted Publishers" in "Local Computer" (not current user)
  - Enable test signing
    - `bcdedit.exe /set TESTSIGNING ON`
    - Reboot the system to apply

### Install USB/IP client
- Download and unpack [release](https://github.com/vadimgrn/usbip-win2/releases)
- Install drivers
  - Run PowerShell or CMD as an Administrator
  - Go to the folder with USBIP binaries and run
  - `usbip.exe install`

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
### Uninstallation of USB/IP client
- Uninstall drivers
  - `usbip.exe uninstall`
- Disable test signing
  - `> bcdedit.exe /set TESTSIGNING OFF`
  - Reboot the system to apply

## Obtaining USBIP logs on Windows
- WPP Software Tracing is used
- Use the tools for software tracing, such as TraceView, Tracelog, Tracefmt, and Tracepdb to configure, start, and stop tracing sessions and to display and filter trace messages
- These tools are included in the Windows Driver Kit (WDK)
- Use these tracing GUIDs
  - `8b56380d-5174-4b15-b6f4-4c47008801a4` for vhci driver
  - `8b56380d-5174-4b15-b6f4-4c47008801a4` for usbip_xfer utility
- Example of a log session for vhci driver using command-line tools
  - Start a new log session
    - `tracelog.exe -start usbip-vhci -guid #8b56380d-5174-4b15-b6f4-4c47008801a4 -f usbip-vhci.etl -flag 0x1F -level 5`
  - Stop the log session
    - `tracelog.exe -stop usbip-vhci`
  - Get readable text from binary event trace log `usbip-vhci.etl`
```
set TMFS=%TEMP%\tmfs
set TRACE_FORMAT_PREFIX=%%4!s! [%%9!2u!]%%3!04x! %%!LEVEL! %%!FUNC!:
tracepdb.exe -f D:\usbip-win2\x64\Debug -p %TMFS%
tracefmt.exe -nosummary -p %TMFS% -o usbip-vhci.txt usbip-vhci.etl
```

## Debugging BSOD
- Enable kernel memory dump
  - Open "System Properties" dialog bog
  - Select "Advanced" tab 
  - Click on "Settings" in "Startup and Recovery"
  - "System failure", "Write debugging information", pick "Kernel Memory Dump"
  - Check "Overwrite any existing file"
- Start WPP tracing session for vhci driver as described in the previous topic
- When BSOD has occured
  - Reboot PC if automatic reboot is not set
  - Run Windows debugger WinDbg.exe as Administrator
  - Press Ctrl+D to open chash dump
  - Run following commands and copy the output 
```
!analyze -v
!wmitrace.logdump usbip-vhci
``` 

## Obtaining USBIP log on Linux
```
killall usbipd
modprobe -r usbip-host usbip-vudc vhci-hcd usbip-core
modprobe usbip-core usbip_debug_flag=0xFFFFFFFF
modprobe -a usbip-host usbip-vudc vhci-hcd
usbipd -D
dmesg --follow | tee ~/usbip.log
```
## Plans for near future
  - Use Cancel-Safe IRP Queues. Issues with IRP cancellation are the major cause of BSODs.
  - Get rid of usbip_xfer.exe userspace app and handle TCP/IP data exchange inside the driver
  - Use vectored I/O (MDL) to avoid copying of buffers
