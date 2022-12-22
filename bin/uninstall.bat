# uninstall
devcon classfilter usb upper !usbip2_filter
pnputil /remove-device /deviceid "ROOT\USBIP_WIN2\VHCI" /subtree
FOR /f %P IN ('findstr /M /L "Manufacturer=\"USBIP-WIN2\"" C:\WINDOWS\INF\oem*.inf') DO pnputil.exe /delete-driver %~nxP /uninstall
pnputil /restart-device /deviceid USB\ROOT_HUB30
del /Q "C:\Program Files\usbip-win2"

# query

devcon classfilter usb upper
pnputil /enum-devices /deviceid "USB\ROOT_HUB30" /stack

devcon hwids ROOT\USBIP_WIN2\*
pnputil /enum-devices /deviceid "ROOT\USBIP_WIN2\VHCI" /stack /properties

pnputil /enum-classes /class usb /services
pnputil /enum-drivers /class usb /files

# install 

cd D:\usbip-win2\x64\Debug\package\

certutil -f -p usbip -importPFX root ..\..\..\drivers\usbip_test.pfx
certutil -store root | findstr "USBIP Test"

pnputil /add-driver usbip2_filter.inf /install
..\devnode install usbip2_vhci.inf ROOT\USBIP_WIN2\VHCI

# Windows 11 version 21H2
pnputil /restart-device /deviceid USB\ROOT_HUB30
