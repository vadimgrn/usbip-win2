set WXDIR=..\..\packages\wxWidgets.3.2.2.1\include\wx
set TOOLS=..\..\tools
set PATCHES=wxpatch
set FNAME=dataview.h

%TOOLS%\sed.exe -i -f %PATCHES%\log.h.sed  %WXDIR%\log.h

%TOOLS%\pathc.exe --unified --forward --reject-file=%TEMP%\%FNAME%.rej --directory=%WXDIR%\persist --input=%CD%\%PATCHES%\%FNAME%.diff
del /f /q %TEMP%\%FNAME%.rej > nul 2>&1
