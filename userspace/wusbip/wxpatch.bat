setlocal
set TRIPLET=%1
set WXDIR=vcpkg_installed\%TRIPLET%\%TRIPLET%\include\wx
set TOOLS=..\..\tools
set REJECT=usbip

rem cd %WXDIR%\.. && diff -urd wx wx_patched > wxpatch.diff

%TOOLS%\pathc.exe -p1 --unified --forward --reject-file=%TEMP%\%REJECT%.rej --directory=%WXDIR% --input=%CD%\wxpatch.diff
del /f /q %TEMP%\%REJECT%.rej > nul 2>&1
