# USB/IP for Windows

- Implements USB/IP client for Windows (vhci driver) which is fully compatible with Linux USB/IP server.
- USB/IP server for Windows (stub driver) is updated from the original repository AS IS.

## Build

### Notes
- Build is tested on Windows 11 x64 and the projects are configured for Win10 target by default.
- x86 platform is no longer supported.

### Build Tools
- The latest Microsoft Visual Studio Community 2019
- Windows 11 SDK (10.1.22000.194)
- Windows Driver Kit (10.1.22000.1)
- vcpkg

### Install Boost C++
- Install vcpkg, https://vcpkg.io/en/getting-started.html
  - git clone https://github.com/Microsoft/vcpkg.git
  - .\vcpkg\bootstrap-vcpkg.bat
- Integrate vcpkg with Visual Studio
  - vcpkg integrate install
- Install Boost C++ libraries
  - vcpkg install boost:x64-windows

### Build Process
- Open `usbip_win.sln`
- Set certificate driver signing for `usbip_stub` and `usbip_vhci` projects
  - Right-click on the `Project > Properties > Driver Signing > Test Certificate`
  - Browse to `driver/usbip_test.pfx` (password: usbip)
- Build solution or desired project
- All output files are created under {Debug,Release}/x64 folder.

## Install

### USB/IP test certificate
  - Right click on `driver/usbip_test.pfx`, select "Install PFX" (password: usbip)
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) *and*
    2. "Trusted Publishers" in "Local Computer" (not current user)
  - Enable test signing
    - `> bcdedit.exe /set TESTSIGNING ON`
    - reboot the system to apply

### Windows USB/IP client
- Copy VHCI driver files into a folder in target machine
  - Copy `usbip.exe`, `usbip_xfer.exe`, `usbip_vhci.sys`, `usbip_vhci.inf`, `usbip_root.inf`, `usbip_vhci.cat` into a folder in target machine;
  - You can find all files in output folder after build or on [release](https://github.com/vadimgrn/usbip-win/releases) page.
- Install USB/IP VHCI driver
  - Run PowerShell or CMD as an Administrator
  - `PS> usbip.exe install`
- Run USB/IP server on Linux machine (or try to use Windows USB/IP server)
  - `$ sudo modprobe -a usbip-core usbip-host`
  - `$ sudo usbipd -D`
- List available USB devices on Linux USB/IP server
```
$ usbip list -l
 - busid 3-2 (1005:b113)
   Apacer Technology, Inc. : Handy Steno/AH123 / Handy Steno 2.0/HT203 (1005:b113)

 - busid 3-3.2 (07ca:513b)
   AVerMedia Technologies, Inc. : unknown product (07ca:513b)
```
- Export desired USB device(s) using its busid on Linux USB/IP server
  - `$ sudo usbip bind -b 3-2`
- Query exported USB devices on Windows using USB/IP client
  - `PS> usbip.exe list -r <usbip server ip>`
```
Exportable USB devices
======================
 - 192.168.1.9
        3-2: unknown vendor : unknown product (1005:b113)
           : /sys/devices/pci0000:00/0000:00:14.0/usb3/3-2
           : (Defined at Interface level) (00/00/00)
```
- Attach desired remote USB device using its busid
  - `PS> usbip.exe attach -r <usbip server ip> -b 3-2`
- New USB device should appear on Windows, use it as usual
- Detach the remote USB device using its usb port, pass -1 to detach all remote devices
  - `PS> usbip.exe detach -p -1`
- Uninstall the driver
  - `PS> usbip.exe uninstall`
- Disable test signing
  - `> bcdedit.exe /set TESTSIGNING OFF`
  - reboot the system to apply

### Windows USB/IP server
- Prepare a Linux machine as a USB/IP client or Windows usbip-win VHCI client
  - `$ sudo modprobe -a usbip-core vhci-hcd`
- Copy `usbip.exe`, `usbipd.exe`, `usbip_xfer.exe`, `usb.ids`, `usbip_stub.sys`, `usbip_stub.inx` into a folder in target machine
  - You can find there files in the output folder after build or on [release](https://github.com/vadimgrn/usbip-win/releases) page.
  - `userspace/usb.ids`
  - `driver/stub/usbip_stub.inx`
- Find USB Device ID
  - You can get id from usbip listing
    - `> usbip.exe list -l`
  - Bus id is always 1. So output from `usbip.exe` listing is shown as:
```
usbip.exe list -l
 - busid 1-59 (045e:00cb)
   Microsoft Corp. : Basic Optical Mouse v2.0 (045e:00cb)
 - busid 1-30 (80ee:0021)
   VirtualBox : USB Tablet (80ee:0021)
```
- Bind USB device to usbip stub
  - The next command replaces the existing function driver with usbip stub driver
    - This should be executed using administrator privilege
    - `usbip_stub.inx` and `usbip_stub.sys` files should be in the same folder as `usbip.exe`
  - `> usbip.exe bind -b 1-59`
- Run `usbipd.exe`
  - `> usbipd.exe`
  - TCP port `3240` should be allowed by firewall
- Attach USB/IP device on Linux machine
  - `# usbip attach -r <usbip server ip> -b 1-59`

### Reporting Bugs
- `usbip-win` is not yet ready for production use. It can cause BSOD or hang Windows Explorer, etc.
- We could find the problems with detailed logs

#### How to get Windows kernel log for drivers
- WPP Software Tracing is used
- Use the tools for software tracing, such as TraceView, Tracelog, Tracefmt, and Tracepdb to configure, start, and stop tracing sessions and to display and filter trace messages
- These tools are included in the Windows Driver Kit (WDK)
- Use these tracing GUIDs
  - `8b56380d-5174-4b15-b6f4-4c47008801a4` for vhci driver
  - `682e9961-054c-482b-a86d-d94f6cd5f555` for stub driver
  - `8b56380d-5174-4b15-b6f4-4c47008801a4` for usbip_xfer utility
- Example of a log session for vhci driver using command-line tools
  - Start a new log session
    - `tracelog.exe -start usbip-vhci -guid #8b56380d-5174-4b15-b6f4-4c47008801a4 -f usbip-vhci.etl -flag 0xF -level 5`
  - Stop the log session
    - `tracelog.exe -stop usbip-vhci`
  - Format binary event trace log `usbip-vhci.etl` as text
```
set TMFS=%TEMP%\tmfs
set TRACE_FORMAT_PREFIX=%%4!s! [%%9!2u!]%%3!04x! %%!LEVEL! %%!FUNC!:
tracepdb.exe -f D:\usbip-win\Debug\x64 -p %TMFS%
tracefmt.exe -nosummary -p %TMFS% -o usbip-vhci.txt usbip-vhci.etl
```
- How to use `TraceView.exe` GUI app
  - Open the menu item "File/Create New Log Session"
  - \"*Provider Control GUID Setup*\" dialog window appears
  - Choose \"*PDB (Debug Information) File*\" radio button and specify PDB file
    - `usbip_stub.pdb` for stub driver
    - `usbip_vhci.pdb` for vhci driver
    - `usbip_xfer.pdb` for usbip_xfer utility
  - You can send real-time trace messages to WinDbg by modifying in \"*Advanced Log Session Options*\".
- If your testing machine suffer from BSOD (Blue Screen of Death), you should get it via remote debugging.
  - `WinDbg` on virtual machines would be good to get logs

#### How to get linux kernel log
- Sometimes Linux kernel log is required
```
# dmesg --follow | tee kernel_log.txt
```
