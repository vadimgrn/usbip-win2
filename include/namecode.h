#pragma once

#define K_V(a) {#a, (unsigned int)a},

 typedef struct namecode
{
	const char *name;
	unsigned int code;
} namecode_t;
