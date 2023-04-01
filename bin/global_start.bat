; Uncomment #define WPP_GLOBALLOGGER in drivers/ude/trace.h to use GlobalLogger

@echo off
set NAME=usbip
set UDE=HKLM\System\CurrentControlSet\Control\WMI\GlobalLogger\{ed18c9c5-8322-48ae-bf78-d01d898a1562}

reg add %UDE%
reg add %UDE% /v Flags /t REG_DWORD /d 0xF /f
reg add %UDE% /v Level /t REG_DWORD /d 5 /f

tracepdb.exe -f "C:\Program Files\USBip\*.pdb" -s -p %TEMP%\%NAME%
tracelog -start GlobalLogger

echo ###
echo ### Reboot the system to start global GlobalLogger session ###
echo ###
