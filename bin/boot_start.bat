@echo off

set NAME=USBip

set LOG=HKLM\System\CurrentControlSet\Control\WMI\Autologger\%NAME%
set LOG_UDE=%LOG%-ude
set LOG_FLT=%LOG%-flt

set PROV_UDE=%LOG_UDE%\{ed18c9c5-8322-48ae-bf78-d01d898a1562}
set PROV_FLT=%LOG_FLT%\{90c336ed-69fb-43d6-b800-1552d72d200b}

reg add %PROV_UDE% /f
reg add %PROV_FLT% /f

reg add %PROV_UDE% /v Enabled /t REG_DWORD /d 1 /f
reg add %PROV_FLT% /v Enabled /t REG_DWORD /d 1 /f

reg add %PROV_UDE% /v EnableLevel /t REG_DWORD /d 5 /f
reg add %PROV_FLT% /v EnableLevel /t REG_DWORD /d 5 /f

reg add %PROV_UDE% /v EnableFlags /t REG_DWORD /d 0xF /f
reg add %PROV_FLT% /v EnableFlags /t REG_DWORD /d 0x3 /f

reg add %LOG_UDE% /v Guid /t REG_SZ /d {6decbb2b-ef32-41e2-be5b-4266e1fe5410} /f
reg add %LOG_FLT% /v Guid /t REG_SZ /d {8440db8a-0637-4712-bdd5-9da510ef14e6} /f

reg add %LOG_UDE% /v Start /t REG_DWORD /d 1 /f
reg add %LOG_FLT% /v Start /t REG_DWORD /d 1 /f

tracepdb.exe -f "C:\Program Files\USBip\*.pdb" -s -p %TEMP%\%NAME%

echo ##########################################
echo ### Reboot the system to start logging ###
echo ##########################################
