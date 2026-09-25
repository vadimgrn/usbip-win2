rem HVCI (Memory Integrity) — Settings > Device Security > Core Isolation > Memory Integrity → Off
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v "Enabled" /t REG_DWORD /d 0 /f

reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Config" /v "VulnerableDriverBlocklistEnable" /t REG_DWORD /d 0 /f

rem Smart App Control — Settings > Privacy & Security > Windows Security > App & browser control > Smart App Control → Off
rem Warning: this is a one-way toggle, can't re-enable without reinstalling Windows.
reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v "VerifiedAndReputablePolicyState" /t REG_DWORD /d 0 /f

rem Virtualization Based Security (VBS) — HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\EnableVirtualizationBasedSecurity → 0
