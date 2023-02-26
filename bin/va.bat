@echo off
set NAME=usbip2

tracelog.exe -stop %NAME%-vhci
tracelog.exe -stop %NAME%-fltr

rm %NAME%-*.*
tracepdb.exe -f "C:\Program Files\usbip-win2\*.pdb" -s -p %TEMP%\%NAME%

tracelog.exe -start %NAME%-vhci -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%-vhci.etl -flag 0xF -level 5
tracelog.exe -start %NAME%-fltr -guid #90c336ed-69fb-43d6-b800-1552d72d200b -f %NAME%-fltr.etl -flag 0x3 -level 5
