cd %1
if exist vhci_ude_cat del /s /q vhci_ude_cat
mkdir vhci_ude_cat
cd vhci_ude_cat
copy ..\usbip_vhci_ude.sys
copy ..\usbip_vhci_ude.inf
inf2cat /driver:.\ /os:%2 /uselocaltime
signtool sign /f %3 /p usbip /fd SHA256 usbip_vhci_ude.cat
copy /y usbip_vhci_ude.cat ..
cd ..
del /s /q vhci_ude_cat
