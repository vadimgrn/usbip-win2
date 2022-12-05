@echo off
set NAME=usbip2-vhci
tracelog.exe -stop %NAME%
rm %NAME%.*
tracepdb.exe -f "C:\Program Files\usbip-win2\*.pdb" -s -p %TEMP%\%NAME%
tracelog.exe -start %NAME% -guid #ed18c9c5-8322-48ae-bf78-d01d898a1562 -f %NAME%.etl -flag 0xF -level 5
