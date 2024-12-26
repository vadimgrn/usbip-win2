@echo off
SET CUR_DIR=%CD%

git.exe submodule update --init --recursive --remote

cd vcpkg
bootstrap-vcpkg.bat -disableMetrics
cd %CUR_DIR%
