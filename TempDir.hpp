#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include <string>


class TempDir final {
	std::string path;

public:
	TempDir() {
		char tempPath[MAX_PATH];
		DWORD len = GetTempPathA(sizeof(tempPath), tempPath);
		if (len == 0 || len > MAX_PATH)
			return;

		char buf[MAX_PATH];
		UINT n = GetTempFileNameA(tempPath, "spc", 0, buf);
		if (n == 0)
			return;

		// Delete the file created by GetTempFileName(). This is not race-free,
		// but good enough for our purpose.
		BOOL ok = DeleteFileA(buf);
		if (!ok)
			return;

		ok = CreateDirectoryA(buf, NULL);
		if (!ok)
			return;

		path = buf;
	}

	~TempDir() {
		if (path.empty())
			return;

		SHFILEOPSTRUCTA deleteOp;
		memset(&deleteOp, 0, sizeof(deleteOp));
		deleteOp.wFunc = FO_DELETE;
		path += '\0'; // Double-null required
		deleteOp.pFrom = path.c_str();
		deleteOp.fFlags = FOF_NO_UI;
		SHFileOperationA(&deleteOp);
	}

	std::string GetPath() {
		return path;
	}
};
