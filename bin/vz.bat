@echo off
set NAME=usbip2-vhci
set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:

tracelog.exe -stop %NAME%
tracefmt.exe -nosummary -p %TEMP%\%NAME% -o %NAME%.txt %NAME%.etl

sed -i "s/TRACE_LEVEL_CRITICAL/CRT/;s/TRACE_LEVEL_ERROR/ERR/;s/TRACE_LEVEL_WARNING/WRN/;s/TRACE_LEVEL_INFORMATION/INF/;s/TRACE_LEVEL_VERBOSE/VRB/" %NAME%.txt
sed -i "s/`anonymous namespace':://" %NAME%.txt
rm sed*
