@echo off

set APPDIR=C:\Program Files\USBip
set HWID=ROOT\USBIP_WIN2\UDE

"%APPDIR%\usbip.exe" detach --all

"%APPDIR%\devnode.exe" remove %HWID% root
rem pnputil.exe /remove-device /deviceid %HWID% /subtree

rem WARNING: use %%P and %%~nxP if you run this command in a .bat file
FOR /f %%P IN ('findstr /M /L %HWID% C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %%~nxP /uninstall

"%APPDIR%\classfilter.exe" uninstall "%APPDIR%\usbip2_filter.inf" DefaultUninstall.NTamd64
rd /S /Q "%APPDIR%"
