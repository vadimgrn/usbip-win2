@echo off

:loop
usbip.exe attach -r pc -b 3-1
usbip.exe detach -a
timeout /t 1 > NUL
goto loop
