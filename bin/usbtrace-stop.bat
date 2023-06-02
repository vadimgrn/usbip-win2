@echo off
set NAME=usbtrace

logman stop -n %NAME%
logman delete -n %NAME%
move /Y %SystemRoot%\Tracing\%NAME%_000001.etl .\%NAME%.etl

set TRACE_FORMAT_PREFIX=[%%9]%%3!04x! %%!LEVEL! %%!FUNC!:
tracefmt.exe -nosummary -o %NAME%.txt %NAME%.etl
