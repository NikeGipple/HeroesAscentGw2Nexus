// MumbleLink.cpp
#include <Windows.h>
#include "MumbleLink.h"
#include "Globals.h"
#include "nexus/Nexus.h"

namespace MumbleLink
{
    Mumble::Data* gMumble = nullptr;

    void Init()
    {
        HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, "MumbleLink");
        if (!hMap)
        {
            if (APIDefs)
                APIDefs->Log(ELogLevel_WARNING, "MumbleLink", "MumbleLink not found");
            return;
        }

        gMumble = (Mumble::Data*)MapViewOfFile(
            hMap, FILE_MAP_READ, 0, 0, sizeof(Mumble::Data));

        if (!gMumble)
        {
            if (APIDefs)
                APIDefs->Log(ELogLevel_WARNING, "MumbleLink", "Failed to map MumbleLink");
            CloseHandle(hMap);
            return;
        }

        if (APIDefs)
            APIDefs->Log(ELogLevel_INFO, "MumbleLink", "MumbleLink initialized");
    }
}
