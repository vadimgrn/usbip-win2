@echo off

:loop
usbip.exe attach -r 192.168.1.15 -b 3-1
timeout /t 1 > NUL
usbip.exe detach -a
timeout /t 1 > NUL
GOTO loop
