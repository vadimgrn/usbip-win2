cd %1
if exist vhci_cat del /s /q vhci_cat
mkdir vhci_cat
cd vhci_cat
copy ..\usbip_vhci.sys
copy ..\usbip_vhci.inf
copy ..\usbip_root.inf
inf2cat /driver:.\ /os:%2 /uselocaltime
signtool sign /f %3 /p usbip /fd SHA256 usbip_vhci.cat
copy /y usbip_vhci.cat ..
cd ..
del /s /q vhci_cat
