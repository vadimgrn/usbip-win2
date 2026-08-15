@echo off
rem run from Visual Studio Developer Prompt

set CODEQL_HOME=D:\codeql-home
set USBIP_HOME=D:\usbip-win2
set UDE_HOME=%USBIP_HOME%\drivers\ude
set PROJ_NAME=usbip2_ude
set CURDIR=%CD%

del /Q /F "%UDE_HOME%\%PROJ_NAME%.sarif" "%UDE_HOME%\%PROJ_NAME%.DVL.XML"
rmdir /S /Q "%CODEQL_HOME%\databases"

"%CODEQL_HOME%\codeql\codeql" pack download microsoft/windows-drivers@1.8.2
"%CODEQL_HOME%\codeql\codeql" database create "%CODEQL_HOME%\databases" --language=cpp --source-root="%UDE_HOME%" --command="msbuild -target:usbip2_ude:rebuild -p:Configuration=Release -p:Platform=x64 -p:EnablePREfast=true -p:RunCodeAnalysis=false \"%USBIP_HOME%\usbip_win2.slnx\""
"%CODEQL_HOME%\codeql\codeql" database analyze "%CODEQL_HOME%\databases" microsoft/windows-drivers@1.8.2:windows-driver-suites/mustrun.qls --download --format=sarif-latest --output="%UDE_HOME%\%PROJ_NAME%.sarif"

cd /d "%UDE_HOME%"
msbuild /t:dvl /p:Configuration="Release" /p:Platform="x64" /p:SarifTelemetryPath="%PROJ_NAME%.sarif" usbip2_ude.vcxproj

powershell -ExecutionPolicy Bypass -Command "$sarif = Get-Content -Raw '%PROJ_NAME%.sarif' | ConvertFrom-Json; $sarif.runs | ForEach-Object { $_.results } | ForEach-Object { $loc = $_.locations | Select-Object -First 1; [PSCustomObject]@{ RuleId = $_.ruleId; File = if ($loc.physicalLocation.artifactLocation.uri) { $loc.physicalLocation.artifactLocation.uri } else { 'N/A' }; Line = if ($loc.physicalLocation.region.startLine) { $loc.physicalLocation.region.startLine } else { 'N/A' } } } | Format-Table -AutoSize"
powershell -Command "Get-Content -Raw '%PROJ_NAME%.sarif' | ConvertFrom-Json | ConvertTo-Json -Depth 100 | Set-Content '%PROJ_NAME%.sarif'"

"C:\Program Files (x86)\Windows Kits\10\Tools\bin\x64\dvl.exe" /manualCreate %PROJ_NAME% x64 /sarifPath "%UDE_HOME%"
cd /d "%CURDIR%"
