# uninstall
devcon hwids ROOT\USBIP_WIN2\*
pnputil /remove-device /deviceid "ROOT\USBIP_WIN2\VHCI" /subtree
FOR /F %P IN ('findstr /M /P /L "Manufacturer=\"USBIP-WIN2\"" C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %~nxP /uninstall
del /Q "C:\Program Files\usbip-win2"

devcon classfilter usb upper
devcon classfilter usb upper +usbip2_filter
devcon classfilter usb upper !usbip2_filter


# install 

cd D:\usbip-win2\x64\Debug\package\

pnputil /add-driver usbip2_filter.inf /install
..\devnode install usbip2_vhci.inf ROOT\USBIP_WIN2\VHCI

certutil -f -p usbip -importPFX root ..\..\..\drivers\usbip_test.pfx
certutil -store root | findstr "USBIP Test"
