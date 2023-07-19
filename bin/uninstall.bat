@echo off

set APPDIR=C:\Program Files\USBip
set FILTER=usbip2_filter

usbip detach --all

classfilter remove upper USB %FILTER%
rem devcon classfilter usb upper !%FILTER%

devnode remove ROOT\USBIP_WIN2\UDE root
rem pnputil /remove-device /deviceid ROOT\USBIP_WIN2\UDE /subtree

rem WARNING: '%' must be doubled if you run this command in a .bat file
FOR /f %%P IN ('findstr /M /L ROOT\USBIP_WIN2\UDE C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %%~nxP /uninstall

rem path with spaces, two commands, it's OK
RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultUninstall 128 %APPDIR%\%FILTER%.inf
RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultUninstall 128 %APPDIR%\%FILTER%.inf

rd /S /Q "%APPDIR%"
