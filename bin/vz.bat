@echo off

rem change to your WDK version
set PATH=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64

set NAME=usbip
set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:

tracelog.exe -stop %NAME%-flt
tracelog.exe -stop %NAME%-ude

tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-flt.txt %NAME%-flt.etl
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-ude.txt %NAME%-ude.etl

sed -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%-*.txt
sed -i "s/`anonymous namespace':://" %NAME%-*.txt
del /F sed*
