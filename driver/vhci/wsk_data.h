#pragma once

#include <ntdef.h>

struct vpdo_dev_t;
struct _WSK_DATA_INDICATION;

bool wsk_data_push(_Inout_ vpdo_dev_t &vpdo, _In_ _WSK_DATA_INDICATION *DataIndication, _In_ size_t BytesIndicated);
NTSTATUS wsk_data_retain_tail(_Inout_ vpdo_dev_t &vpdo);
bool wsk_data_pop(_Inout_ vpdo_dev_t &vpdo);

size_t wsk_data_size(_In_ const vpdo_dev_t &vpdo);
NTSTATUS wsk_data_copy(_Out_ void *dest, _In_ size_t len, _Inout_ vpdo_dev_t &vpdo);
void wsk_data_release(_Inout_ vpdo_dev_t &vpdo, _In_ size_t len);
