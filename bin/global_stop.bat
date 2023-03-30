@echo off
set NAME=usbip
set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:
set GL=HKLM\System\CurrentControlSet\Control\WMI\GlobalLogger
set ETL=C:\Windows\System32\LogFiles\WMI\GlobalLogger.etl

tracelog -stop GlobalLogger
reg add %GL% /v Start /t REG_DWORD /d 0 /f

tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-ude.txt %ETL%

sed -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%-*.txt
sed -i "s/`anonymous namespace':://" %NAME%-*.txt
rm sed*
