@echo off

logman create trace -n usbtrace -o %SystemRoot%\Tracing\usbtrace.etl -nb 128 640 -bs 128
logman update trace -n usbtrace -p Microsoft-Windows-USB-USBXHCI (Default,PartialDataBusTrace)
logman update trace -n usbtrace -p Microsoft-Windows-USB-UCX (Default,HeadersBusTrace,FullDataBusTrace,IRP,HWVerifyHost,HWVerifyHub,HWVerifyDevice)
logman update trace -n usbtrace -p Microsoft-Windows-USB-USBHUB3 (Default,PartialDataBusTrace)
logman update trace -n usbtrace -p Microsoft-Windows-USB-USBPORT
logman update trace -n usbtrace -p Microsoft-Windows-USB-USBHUB
logman update trace -n usbtrace -p Microsoft-Windows-Kernel-IoTrace 0 2
logman start -n usbtrace
