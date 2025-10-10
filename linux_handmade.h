/* date = April 15th 2025 4:50 pm */

#ifndef LINUX_HANDMADE_H
#define LINUX_HANDMADE_H

struct linux_game_code
{
    game_update_and_render *UpdateAndRender;
    game_get_sound_samples *GetSoundSamples;
    
    void *LibraryHandle;
    struct timespec LibraryLastWriteTime;
};

enum linux_gamepad_axes_enum
{
    LSTICKX,
    LSTICKY,
    RSTICKX,
    RSTICKY,
    LSHOULDER,
    RSHOULDER,
    DPADX,
    DPADY,
    AXES_COUNT
};

struct linux_gamepad_axis
{
    s32 Minimum;
    s32 Maximum;
    s32 Fuzz;
    s32 Flat;
};

struct linux_gamepad
{
    // TODO(luca): rename to FileHandle
    int File;
    char FilePath[PATH_MAX];
    
    char Name[256];
    int SupportsRumble;
    linux_gamepad_axis Axes[AXES_COUNT];
};

struct linux_replay_buffer
{
    size_t Pos;
    size_t Size;
    char *Memory;
    size_t MemorySize;
};
struct linux_state
{
    Display *DisplayHandle;
    
    b32 IsFullScreen;
    
    int InputPlayingIndex;
    int InputRecordingIndex;
    linux_replay_buffer ReplayBuffers[5];
    
    char ExecutablePath[PATH_MAX];
    
    size_t TotalSize;
    void *GameMemoryBlock;
};

struct linux_thread_info
{
    u64 LogicalThreadIndex;
    platform_work_queue *Queue;
};

struct platform_work_queue
{
    sem_t SemaphoreHandle;
    
    volatile u32 CompletionGoal;
    volatile u32 CompletionCount;
    
    volatile u32 NextEntryToWrite;
    volatile u32 NextEntryToRead;
    platform_work_queue_entry Entries[256];
};

//- Prototypes 
// for sapall
internal struct timespec LinuxGetWallClock(void);

#endif //LINUX_HANDMADE_H
