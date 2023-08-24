@echo off

set HWID=ROOT\USBIP_WIN2\UDE
set APPDIR=C:\Program Files\USBip
set OEMDIR=C:\WINDOWS\INF\oem*.inf

"%APPDIR%\usbip.exe" detach --all

"%APPDIR%\devnode.exe" remove %HWID% root
rem pnputil.exe /remove-device /deviceid %HWID% /subtree

rem WARNING: '%' must be doubled if you run this command in a .bat file
FOR /f %%P IN ('findstr /M /L %HWID% %OEMDIR%') DO pnputil.exe /delete-driver %%~nxP /uninstall
FOR /f %%P IN ('findstr /M /L usbip2_filter.cat %OEMDIR%') DO "%APPDIR%\classfilter.exe" uninstall "%%P" DefaultUninstall.NTamd64 & pnputil.exe /delete-driver %%~nxP

rd /S /Q "%APPDIR%"
