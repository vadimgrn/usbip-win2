@echo off

set NAME=USBip
set NAME_UDE=%NAME%-ude
set NAME_FLT=%NAME%-flt

set REG_DIR=HKLM\System\CurrentControlSet\Control\WMI\Autologger
set LOG_DIR=%SystemRoot%\System32\LogFiles\WMI

set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:

tracelog.exe -stop %NAME_UDE%
tracelog.exe -stop %NAME_FLT%

tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME_UDE%.txt %LOG_DIR%\%NAME_UDE%.etl
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME_FLT%.txt %LOG_DIR%\%NAME_FLT%.etl

sed.exe -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%-*.txt
sed.exe -i "s/`anonymous namespace':://" %NAME%-*.txt
del /F /Q sed*

reg delete %REG_DIR%\%NAME_UDE% /f
reg delete %REG_DIR%\%NAME_FLT% /f
