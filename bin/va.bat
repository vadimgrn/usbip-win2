@echo off
set NAME=usbip

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

del /F %NAME%-*.*
tracepdb.exe -f "C:\Program Files\USBip\*.pdb" -s -p %TEMP%\%NAME%

tracelog.exe -start %NAME%-flt -guid #90c336ed-69fb-43d6-b800-1552d72d200b -f %NAME%-flt.etl -flag 0x3 -level 5
tracelog.exe -start %NAME%-ude -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%-ude.etl -flag 0xF -level 5
