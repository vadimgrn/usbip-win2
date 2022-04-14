#pragma once

#include <windows.h>

int sign_file(LPCSTR subject, LPCSTR fpath);
BOOL has_certificate(LPCSTR subject);
