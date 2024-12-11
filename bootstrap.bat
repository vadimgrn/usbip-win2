@echo off

set GIT=git.exe
SET CUR_DIR=%CD%

%GIT% submodule update --init --recursive

cd vcpkg
%GIT% checkout master
%GIT% pull
bootstrap-vcpkg.bat -disableMetrics
cd %CUR_DIR%
