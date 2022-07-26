#include "ZipCompress.h"

#include <zip.h>

#include <stdlib.h>
#include <string.h>

struct InMemoryZip {
    zip_source_t *bufferSrc;
};

struct InMemoryZip *CreateInMemoryZip() {
    return calloc(1, sizeof(struct InMemoryZip));
}

void FreeInMemoryZip(struct InMemoryZip *imz) {
    if (!imz)
        return;
    zip_source_free(imz->bufferSrc);
    imz->bufferSrc = NULL;
}

int CompressToInMemoryZip(const void *input, size_t inSize,
                          struct InMemoryZip *imz, const char *filename,
                          int level) {
    zip_error_t error;
    zip_error_init(&error);

    // Note that libzip objects are reference counted, so we don't always need
    // to explicitly free them.

    imz->bufferSrc = zip_source_buffer_create(0, 0, 1, &error);
    if (!imz->bufferSrc) {
        int err = error.zip_err;
        zip_error_fini(&error);
        return err;
    }

    zip_t *archive =
        zip_open_from_source(imz->bufferSrc, ZIP_TRUNCATE, &error);
    if (!archive) {
        zip_source_free(imz->bufferSrc);
        int err = error.zip_err;
        zip_error_fini(&error);
        return err;
    }

    zip_error_fini(&error);

    zip_source_t *inputSrc = zip_source_buffer(archive, input, inSize, 0);
    if (!inputSrc) {
        int err = zip_get_error(archive)->zip_err;
        zip_close(archive);
        return err;
    }

    zip_int64_t index = zip_file_add(archive, filename, inputSrc, 0);
    if (index < 0) {
        int err = zip_get_error(archive)->zip_err;
        zip_source_free(inputSrc);
        zip_close(archive);
        return err;
    }

    if (zip_set_file_compression(archive, index, ZIP_CM_DEFLATE, level)) {
        int err = zip_get_error(archive)->zip_err;
        zip_source_free(inputSrc);
        zip_close(archive);
        return err;
    }

    zip_source_keep(imz->bufferSrc);
    zip_close(archive);

    return 0;
}

int WriteInMemoryZipToFile(struct InMemoryZip *imz, FILE *fp) {
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_source_stat(imz->bufferSrc, &stat) < 0) {
        return 1;
    }
    size_t size = stat.size;

    if (zip_source_open(imz->bufferSrc) < 0) {
        return zip_source_error(imz->bufferSrc)->zip_err;
    }

    int err = 0;
    size_t bufSize = 64 * 1024;
    void *buffer = malloc(bufSize);
    if (!buffer) {
        err = 1; // Out of memory
        goto exit;
    }

    while (size > 0) {
        size_t readSize = zip_source_read(imz->bufferSrc, buffer, bufSize);
        if (readSize == 0) {
            err = 1; // Unexpected error
            goto exit;
        }
        size_t written = fwrite(buffer, 1, readSize, fp);
        if (written < readSize) {
            err = 1; // Write error
            goto exit;
        }
        size -= readSize;
    }

exit:
    free(buffer);
    zip_source_close(imz->bufferSrc);
    return err;
}
