devcon hwids ROOT\USBIP_WIN2\*
pnputil /remove-device /deviceid ROOT\USBIP_WIN2\* /subtree
FOR /F %P IN ('findstr /m ROOT\USBIP2_VHCI C:\Windows\INF\oem*.inf') DO pnputil.exe /delete-driver %~nxP /uninstall
del /Q "C:\Program Files\usbip-win2"
