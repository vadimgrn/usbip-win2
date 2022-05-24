#pragma once

#include <ntdef.h>

struct vpdo_dev_t;
struct _WSK_DATA_INDICATION;

void wsk_data_push(_Inout_ vpdo_dev_t &vpdo, _In_ _WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated);
NTSTATUS wsk_data_copy(_In_ const vpdo_dev_t &vpdo, _Out_ void *dest, _In_ size_t offset, _In_ size_t len, _Out_ size_t *actual = 0);
size_t wsk_data_consume(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len);

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo);
