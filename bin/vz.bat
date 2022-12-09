@echo off
set NAME=usbip2
set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:

tracelog.exe -stop %NAME%-vhci
tracelog.exe -stop %NAME%-fltr

tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-vhci.txt %NAME%-vhci.etl
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%-fltr.txt %NAME%-fltr.etl

sed -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%-vhci.txt
sed -i "s/`anonymous namespace':://" %NAME%-vhci.txt
rm sed*
