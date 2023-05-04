@echo off

devcon classfilter usb upper !usbip2_filter
pnputil /remove-device /deviceid ROOT\USBIP_WIN2\UDE /subtree
FOR /f %%P IN ('findstr /M /L "Manufacturer=\"USBIP-WIN2\"" C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %%~nxP /uninstall
rd /S /Q "C:\Program Files\USBip"
