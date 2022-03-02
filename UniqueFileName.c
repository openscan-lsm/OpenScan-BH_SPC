#include "UniqueFileName.h"

#include <windows.h>
#include <Shlwapi.h>

#include <stdio.h>
#include <stdint.h>

const char* UniqueFileName(const char* prefix, const char* const* extensions, size_t ext_size, char* result, size_t result_size) {
	for (int i = 0; i < 10000; i++) {
		int formatted_length = snprintf(result, result_size, "%s_%04d", prefix, i);
		if (formatted_length < 0 || formatted_length >= result_size) {
			return NULL;
		}

		BOOL any_file_exists = FALSE;
		for (int j = 0; j < ext_size; j++) {
			char file_name[1024];
			snprintf(file_name, sizeof(file_name), "%s%s", result, extensions[j]);
			if (PathFileExistsA(file_name)) {
				any_file_exists = TRUE;
				break;
			}
		}

		if (!any_file_exists) {
			return result;
		}
	}
	return NULL;
}
