for %%i in (fdPHost FDResPub SSDPSRV upnphost) do sc config %%i start=auto && sc start %%i

reg add HKLM\SYSTEM\CurrentControlSet\Services\LanmanWorkstation\Parameters\ /f /v RequireSecuritySignature /t REG_DWORD /d 0
reg add HKLM\SOFTWARE\Policies\Microsoft\Windows\LanmanWorkstation /f /v AllowInsecureGuestAuth /t REG_DWORD /d 1
reg add HKLM\SOFTWARE\WOW6432Node\Policies\Microsoft\Windows\LanmanWorkstation /f /v AllowInsecureGuestAuth /t REG_DWORD /d 1
