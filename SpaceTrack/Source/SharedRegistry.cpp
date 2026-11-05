#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "SharedRegistry.h"

// The test program sets its own name, so it can never reach a running FL Studio's tracks
#ifndef SPACETRACK_MAPPING_NAME
 #define SPACETRACK_MAPPING_NAME "Local\\SpaceTrackRegistry_v4"
#endif

SharedRegistry* getSharedRegistry()
{
    // Each DLL maps the same named block. New blocks are zero-filled,
    // and all-zero is a valid "everything free" state.
    static SharedRegistry* instance = []() -> SharedRegistry*
    {
        HANDLE h = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, (DWORD) sizeof (SharedRegistry),
                                       SPACETRACK_MAPPING_NAME);
        if (h == nullptr)
            return nullptr;

        void* p = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedRegistry));
        if (p == nullptr)
        {
            CloseHandle (h);
            return nullptr;
        }
        return static_cast<SharedRegistry*> (p);   // handle stays open until the process ends
    }();
    return instance;
}
