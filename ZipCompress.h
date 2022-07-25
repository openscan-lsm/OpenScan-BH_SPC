#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct InMemoryZip;

struct InMemoryZip *CreateInMemoryZip(void);
void FreeInMemoryZip(struct InMemoryZip *imz);
int CompressToInMemoryZip(const void *input, size_t inSize,
                          struct InMemoryZip *imz, const char *filename,
                          int level);
int WriteInMemoryZipToFile(struct InMemoryZip *imz, FILE *fp);

#ifdef __cplusplus
} // extern "C"
#endif
