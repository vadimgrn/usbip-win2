/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

#include "codeseg.h"

namespace wdm
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGED void *GetDeviceProperty(_In_ DEVICE_OBJECT *obj, _In_ DEVICE_REGISTRY_PROPERTY prop, _Out_ NTSTATUS &error, 
                              _Inout_ ULONG &ResultLength);

} // namespace wdm
