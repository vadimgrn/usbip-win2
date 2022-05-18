#pragma once

#include <ntdef.h>

struct vpdo_dev_t;
struct _WSK_DATA_INDICATION;

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo);
_WSK_DATA_INDICATION *wsk_data_back(_In_ const vpdo_dev_t &vpdo);

bool wsk_data_push(_Inout_ vpdo_dev_t &vpdo, _In_ _WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated);
bool wsk_data_pop(_Inout_ vpdo_dev_t &vpdo, _In_ bool release_last = true);

NTSTATUS wsk_data_copy(_Inout_ vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t len);
void wsk_data_consume(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len);
