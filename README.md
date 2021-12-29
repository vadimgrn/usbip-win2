# USB/IP for Windows

- This project aims to support both a USB/IP server and a client on Windows platform.


## Build

### Notes
- Build is tested on Windows 11 x64 and the projects are configured for Win10 target by default.
- x86 platform is no longer supported.

### Build Tools
- The latest Microsoft Visual Studio Community 2019
- Windows 11 SDK (10.1.22000.194)
- Windows Driver Kit (10.1.22000.1)

### Build Process
- Open `usbip_win.sln`
- Set certificate driver signing for `usbip_stub` and `usbip_vhci` projects
  - Right-click on the `Project > Properties > Driver Signing > Test Certificate`
  - Browse to `driver/usbip_test.pfx` (password: usbip)
- Build solution or desired project
- All output files are created under {Debug,Release}/x64 folder.

## Install

### Windows USB/IP server
- Prepare a Linux machine as a USB/IP client or Windows usbip-win VHCI client (tested on Ubuntu 16.04 with kernel 5.13.0-22 - USB/IP kernel module crash was observed on some other versions)
  - `# modprobe vhci-hcd`
- Install USB/IP test certificate
  - Install `driver/usbip_test.pfx` (password: usbip)
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) *and*
    2. "Trusted Publishers" in "Local Computer" (not current user)
- Enable test signing
  - `> bcdedit.exe /set TESTSIGNING ON`
  - reboot the system to apply
- Copy `usbip.exe`, `usbipd.exe`, `usb.ids`, `usbip_stub.sys`, `usbip_stub.inx` into a folder in target machine
  - You can find `usbip.exe`, `usbipd.exe`, `usbip_stub.sys` in the output folder after build or on [release](https://github.com/vadimgrn/usbip-win/releases) page.
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
  - `> usbipd.exe -d -4`
  - TCP port `3240` should be allowed by firewall
- Attach USB/IP device on Linux machine
  - `# usbip attach -r <usbip server ip> -b 1-59`

### Windows USB/IP client
- Prepare a Linux machine as a USB/IP server or Windows usbip-win stub server - (tested on Ubuntu 16.04 (kernel 4.15.0-29), 18.04, 20.04)
  - `# modprobe usbip-host`
  - You can use virtual [usbip-vstub](https://github.com/vadimgrn/usbip-vstub) as a stub server
- Run usbipd on a USB/IP server (Linux)
  - `# usbipd -4 -d`
- Install USB/IP test certificate
  - Install `driver/usbip_test.pfx` (password: usbip)
  - Certificate should be installed into
    1. "Trusted Root Certification Authority" in "Local Computer" (not current user) *and*
    2. "Trusted Publishers" in "Local Computer" (not current user)
- Enable test signing
  - `> bcdedit.exe /set TESTSIGNING ON`
  - reboot the system to apply
- Copy VHCI driver files into a folder in target machine
  - Copy `usbip.exe`, `usbip_vhci.sys`, `usbip_vhci.inf`, `usbip_root.inf`, `usbip_vhci.cat` into a folder in target machine;
  - You can find all files in output folder after build or on [release](https://github.com/vadimgrn/usbip-win/releases) page.
- Install USB/IP VHCI driver
  - You can install using `usbip.exe` or manually
  - Using `usbip.exe` install command
    - Run PowerShell or CMD as an Administrator
    - `PS> usbip.exe install`
  - Manual Installation
    - Run PowerShell or CMD as an Administrator
    - `PS> pnputil /add-driver usbip_vhci.inf`
    - Start Device manager
    - Choose "Add Legacy Hardware" from the "Action" menu.
    - Select "Install the hardware that I manually select from the list".
    - Click "Next".
    - Click "Have Disk", click "Browse", choose the copied folder, and click "OK".
    - Click on the "USB/IP VHCI Root", and then click "Next".
    - Click Finish at "Completing the Add/Remove Hardware Wizard".
- Attach a remote USB device
  - `PS> usbip.exe attach -r <usbip server ip> -b 2-2`
- Uninstall driver
  - `PS> usbip.exe uninstall`
- Disable test signing
  - `> bcdedit.exe /set TESTSIGNING OFF`
  - reboot the system to apply

### Reporting Bugs
- `usbip-win` is not yet ready for production use. We could find the problems with detailed logs.

#### How to get Windows kernel log for drivers
- Drivers use WPP Software Tracing
- Use the tools for software tracing, such as TraceView, Tracelog, Tracefmt, and Tracepdb to configure, start, and stop tracing sessions and to display and filter trace messages
- These tools are included in the Windows Driver Kit (WDK)
- Use these tracing GUIDs
  - `8b56380d-5174-4b15-b6f4-4c47008801a4` for vhci driver
  - `682e9961-054c-482b-a86d-d94f6cd5f555` for stub driver
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
  - You can send real-time trace messages to WinDbg by modifying in \"*Advanced Log Session Options*\".
- If your testing machine suffer from BSOD (Blue Screen of Death), you should get it via remote debugging.
  - `WinDbg` on virtual machines would be good to get logs

#### How to get usbip forwarder log
- usbip-win transmits usbip packets via a userland forwarder.
  - forwarder log is the best to look into usbip packet internals.
- edit `usbip_forward.c` to define `DEBUG_PDU` at the head of the file
- compile `usbip.exe` or `usbipd.exe`
- `debug_pdu.log` is created at the path where an executable runs.

#### How to get linux kernel log
- Sometimes Linux kernel log is required
```
# dmesg --follow | tee kernel_log.txt
```
