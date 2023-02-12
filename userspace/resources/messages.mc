;#pragma once
;//
;// Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>

MessageIdTypedef=UINT32

FacilityNames=(System=0x0FF:FACILITY_SYSTEM
Driver=0x100:FACILITY_DRIVER
Library=0x101:FACILITY_LIBRARY
Device=0x102:FACILITY_DEVICE)

LanguageNames=(English=0x409:MSG00409)

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_GENERAL
Language=English
Driver command completed unsuccessfully.
.

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_ADDRINFO
Language=English
Can't get address information for hostname:port.
.

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_CONNECT
Language=English
Can't connect to the server.
.

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_NETWORK
Language=English
Network error during the data exchange with a remote host.
.

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_VERSION
Language=English
Incompatible USB/IP protocol version.
.

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_PROTOCOL
Language=English
USB/IP protocol violation.
.

MessageId=
Severity=Error
Facility=Driver
SymbolicName=ERROR_USBIP_PORTFULL
Language=English
No free port on USB/IP hub.
.

MessageId=
Severity=Error
Facility=Device
SymbolicName=ERROR_USBIP_ST_NA
Language=English
Device not available.
.

MessageId=
Severity=Error
Facility=Device
SymbolicName=ERROR_USBIP_ST_DEV_BUSY
Language=English
Device busy (already exported).
.

MessageId=
Severity=Error
Facility=Device
SymbolicName=ERROR_USBIP_ST_DEV_ERR
Language=English
Device in error state.
.

MessageId=
Severity=Error
Facility=Device
SymbolicName=ERROR_USBIP_ST_NODEV
Language=English
Device not found by bus id.
.

MessageId=
Severity=Error
Facility=Device
SymbolicName=ERROR_USBIP_ST_ERROR
Language=English
Device unexpected response.
.

MessageId=
Severity=Error
Facility=Application
SymbolicName=ERROR_USBIP_VHCI_NOT_FOUND
Language=English
VHCI device not found, driver not loaded?
.
