#pragma once
#include <atomic>
#include <cstdint>

// One slot per SpaceTrack instance, kept in Windows shared memory so that
// SpaceTrack.vst3 and SpaceMaster.vst3 (separate DLLs) can both see it.
struct SharedSlot
{
    // ---- published by the track ----
    std::atomic<uint64_t> owner;      // 0 = free, otherwise the owning instance's id
    std::atomic<int64_t>  heartbeat;  // last check-in (ms since epoch)
    std::atomic<float>    x, y, z;    // position (mirror of the track's parameters)
    std::atomic<uint32_t> mode3d;     // 0 = 2D, 1 = 3D
    std::atomic<uint32_t> color;      // ARGB, 0 = automatic
    char name[32];                    // UTF-8

    // ---- requests from the master ----
    std::atomic<float>    cmdX, cmdY, cmdZ;
    std::atomic<uint32_t> cmdSeq;
    std::atomic<uint32_t> cmdMode, modeSeq;
    std::atomic<uint32_t> cmdColor, colorSeq;
    char cmdName[32];
    std::atomic<uint32_t> nameSeq;

    // ---- drag state (lets the track group a whole drag into ONE undo step) ----
    std::atomic<uint32_t> dragging;
    std::atomic<int64_t>  dragBeat;
};

struct SharedRegistry
{
    static constexpr int maxSlots = 64;
    SharedSlot slots[maxSlots];

    // ---- shared room, written by SpaceMaster, used by every track in 3D ----
    std::atomic<float>   roomSize, roomAmount;     // 0..1
    std::atomic<int64_t> roomBeat;                 // last check-in (ms since epoch), 0 = no SpaceMaster yet
};

static_assert (std::atomic<uint64_t>::is_always_lock_free, "needs lock-free 64-bit atomics");
static_assert (std::atomic<float>::is_always_lock_free,    "needs lock-free float atomics");

SharedRegistry* getSharedRegistry();
