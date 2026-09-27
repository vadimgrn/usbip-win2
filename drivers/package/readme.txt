Password is 'usbip'.

rem Generate test certificate
makecert -r -pe -a sha256 -len 2048 -ss PrivateCertStore -sr LocalMachine -n "CN=USBip" -eku 1.3.6.1.5.5.7.3.3 -sv usbip.pvk usbip.cer

rem Convert to PFX
set PASSWD=usbip
pvk2pfx -pvk usbip.pvk /pi %PASSWD% -spc usbip.cer -pfx usbip.pfx /f

rem Find installed certificate in the store
certutil -store root | findstr "USBip"


To create the required .CAB files for attestation signing, use the following command at the repository root:
makecab /f ./drivers/package/makecab-arm64.ddf
makecab /f ./drivers/package/makecab-amd64.ddf

The files will be generated in the respective `Release` directories for each architecture under the name `usbip_win2_arm64.cab` for ARM64 and `usbip_win2_amd64.cab` for x64. These files are signed and then submitted for attestation signing.