Password is 'usbip'.

rem Generate test certificate
makecert -r -pe -a sha256 -len 2048 -ss PrivateCertStore -sr LocalMachine -n "CN=USBip" -eku 1.3.6.1.5.5.7.3.3 -sv usbip.pvk usbip.cer

rem Convert to PFX
set PASSWD=decline
pvk2pfx -pvk usbip.pvk /pi %PASSWD% -spc usbip.cer -pfx usbip.pfx /f

rem Find installed certificate in the store
certutil -store root | findstr "USBip"

