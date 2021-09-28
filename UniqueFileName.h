#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* UniqueFileName(const char* prefix, const char* const* extensions, size_t ext_size, char* result, size_t result_size);

#ifdef __cplusplus
}
#endif
