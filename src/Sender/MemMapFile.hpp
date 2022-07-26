#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstddef>
#include <string>

class MemMapFile final {
    HANDLE hFile = NULL;
    HANDLE hMapping = NULL;
    void *mapped = NULL;

    std::string const path;

  public:
    MemMapFile(std::size_t size, std::string const &path) : path(path) {
        hFile = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (!hFile)
            return;

        hMapping = CreateFileMappingA(hFile, NULL, PAGE_READWRITE, size >> 32,
                                      size & 0xffffffff, NULL);
        if (!hMapping)
            return;

        mapped = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    }

    ~MemMapFile() {
        if (mapped) {
            UnmapViewOfFile(mapped);
        }

        if (hMapping) {
            CloseHandle(hMapping);
        }

        if (hFile) {
            CloseHandle(hFile);
        }
    }

    char *Get() { return static_cast<char *>(mapped); }
};
