@echo off

:loop
usbip.exe attach -r pc -b 3-1
rem timeout /t 1 > NUL
usbip.exe detach -a
goto loop 
