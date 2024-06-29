/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that app can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_server,
    0x2b97934b,0x8a70,0x44f8,0x82,0x29,0xcb,0x29,0x96,0x3f,0x37,0x78);
// {2b97934b-8a70-44f8-8229-cb29963f3778}
