/* date = May 12th 2025 10:53 am */

#ifndef HANDMADE_PLATFORM_H
#define HANDMADE_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif
    
    /*
      NOTE(casey):
    
      HANDMADE_INTERNAL:
        0 - Build for public release
        1 - Build for developer only
    
      HANDMADE_SLOW:
        0 - Not slow code allowed!
        1 - Slow code welcome.
    */
    
#include <stdint.h>
#include <stddef.h>
    
    // Zero
#if !defined(COMPILER_MSVC)
# define COMPILER_MSVC 0
#endif
#if !defined(COMPILER_LLVM)
# define COMPILER_LLVM 0
#endif
#if !defined(COMPILER_GNU)
# define COMPILER_GNU 0
#endif
    
    // Detect compiler
#if __clang__
# undef COMPILER_CLANG
# define COMPILER_CLANG 1
#elif _MSC_VER
# undef COMPILER_MSVC
# define COMPILER_MSVC 1
#elif __GNUC__
# undef COMPILER_GNU
# define COMPILER_GNU 1
#else
# error "Could not detect compiler."
#endif
    
#if __MINGW32__
# define COMPILER_MINGW 1
#endif
    
    // Push/Pop warnings
#if defined(COMPILER_GNU)
# define PUSH_WARNINGS \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Weverything\"") \
_Pragma("GCC diagnostic ignored \"-Wconversion\"")
# define POP_WARNINGS _Pragma("GCC diagnostic pop")
    
#elif defined(COMPILER_CLANG)
# define PUSH_WARNINGS \
_Pragma("clang diagnostic push") \
_Pragma("clang diagnostic ignored \"-Weverything\"")
# define POP_WARNINGS _Pragma("clang diagnostic pop")
    
#else
# error "No compatible compiler found"
#endif
    
#define internal static 
#define local_persist static 
#define global_variable static
    
#define Pi32 3.14159265359f
    
    // TODO(casey): Complete assertion macro - don't worry everyone!
#if HANDMADE_SLOW
# if HANDMADE_INTERNAL && OS_LINUX
#   define Assert(Expression) if(!(Expression)) { __asm__ volatile("int3"); } 
# else
#  define Assert(Expression) if(!(Expression)) {*(int *)0 = 0; }
# endif
#else
# define Assert(Expression)
#endif
    
#define DebugBreakOnce do { local_persist b32 X = false; Assert(X); X = true; } while(0)
    
#define NullExpression { int X = 0; }
    
#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)
#define Terabytes(Value) (Gigabytes(Value)*1024LL)
    
#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
#define Min(A, B) (((A) < (B)) ? (A) : (B))
#define Max(A, B) (((A) > (B)) ? (A) : (B))
    
    // TODO(casey): swap, min, max ... macros???
    
    typedef int8_t s8;
    typedef int16_t s16;
    typedef int32_t s32;
    typedef int64_t s64;
    typedef s32 b32;
    
    typedef uint8_t u8;
    typedef uint16_t u16;
    typedef uint32_t u32;
    typedef uint64_t u64;
    
    typedef size_t psize;
    typedef s32 rune;
    
    typedef float r32;
    typedef double r64;
    
    typedef struct thread_context
    {
        int Placeholder;
    } thread_context;
    
    /*
      NOTE(casey): Services that the platform layer provides to the game
    */
    /* IMPORTANT(casey):
    
       These are NOT for doing anything in the shipping game - they are
       blocking and the write doesn't protect against lost data!
    */
    struct debug_platform_read_file_result
    {
        psize ContentsSize;
        void *Contents;
    };
    
#define DEBUG_PLATFORM_FREE_FILE_MEMORY(Name) void Name(thread_context *Thread, void *Memory, psize MemorySize)
    typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);
    
#define DEBUG_PLATFORM_READ_ENTIRE_FILE(Name) debug_platform_read_file_result Name(thread_context *Thread, char *FileName)
    typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);
    
#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(Name) b32 Name(thread_context *Thread, char *FileName, psize MemorySize, void *Memory)
    typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);
    
#define PLATFORM_RUN_COMMAND_AND_GET_OUTPUT(Name) psize Name(thread_context *Thread, char *OutputBuffer, char *Command[])
    typedef PLATFORM_RUN_COMMAND_AND_GET_OUTPUT(platform_run_command_and_get_output);
    
#define PLATFORM_GET_WALL_CLOCK(Name) struct timespec Name(void)
    typedef PLATFORM_GET_WALL_CLOCK(platform_get_wall_clock);
    
#define PLATFORM_LOG(Name) void Name(char *Text)
    typedef PLATFORM_LOG(platform_log);
    
#define PLATFORM
    
    /*
      NOTE(casey): Services that the game provides to the platform layer.
      (this may expand in the future - sound on separate thread, etc.)
    */
    
    // FOUR THINGS - timing, controller/keyboard input, bitmap buffer to use, sound buffer to use
    
    // TODO(casey): In the future, rendering _specifically_ will become a three-tiered abstraction!!!
    typedef struct game_offscreen_buffer
    {
        // NOTE(casey): Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
        void *Memory;
        s32 Width;
        s32 Height;
        s32 Pitch;
        s32 BytesPerPixel;
    } game_offscreen_buffer;
    
    typedef struct game_sound_output_buffer
    {
        s32 SamplesPerSecond;
        s32 SampleCount;
        s16 *Samples;
    } game_sound_output_buffer;
    
    typedef struct game_text_button
    {
        rune Codepoint;
        // TODO(luca): Use flag and bits.
        b32 Control;
        b32 Shift;
        b32 Alt;
    } game_text_button;
    
    typedef struct game_button_state
    {
        s32 HalfTransitionCount;
        b32 EndedDown;
    } game_button_state;
    
    typedef struct game_controller_input
    {
        b32 IsConnected;
        
        struct
        {
            u32 Count;
            game_text_button Buffer[64];
        } Text;
        
        b32 IsAnalog;    
        r32 StickAverageX;
        r32 StickAverageY;
        
        union
        {
            game_button_state Buttons[12];
            struct
            {
                game_button_state MoveUp;
                game_button_state MoveDown;
                game_button_state MoveLeft;
                game_button_state MoveRight;
                
                game_button_state ActionUp;
                game_button_state ActionDown;
                game_button_state ActionLeft;
                game_button_state ActionRight;
                
                game_button_state LeftShoulder;
                game_button_state RightShoulder;
                
                game_button_state Back;
                game_button_state Start;
                
                // NOTE(casey): All buttons must be added above this line
                
                game_button_state Terminator;
            };
        };
    } game_controller_input;
    
    typedef enum
    {
        PlatformCursorShape_None = 0,
        PlatformCursorShape_Grab,
    } platform_cursor_shape;
    
    typedef enum
    {
        PlatformMouseButton_Left = 0,
        PlatformMouseButton_Right,
        PlatformMouseButton_Middle,
        PlatformMouseButton_ScrollUp,
        PlatformMouseButton_ScrollDown,
        PlatformMouseButton_Count
    } platform_mouse_buttons;
    
    typedef struct game_input
    {
        game_button_state MouseButtons[PlatformMouseButton_Count];
        s32 MouseX, MouseY, MouseZ;
        
        r32 dtForFrame;
        
        game_controller_input Controllers[5];
    } game_input;
    
    inline b32 WasPressed(game_button_state State)
    {
        b32 Result = ((State.HalfTransitionCount > 1) || 
                      (State.HalfTransitionCount == 1 && State.EndedDown));
        return Result;
    }
    
    //- Threading 
    typedef struct platform_work_queue platform_work_queue;
    
#define PLATFORM_WORK_QUEUE_CALLBACK(Name) void Name(platform_work_queue *Queue, void *Data)
    typedef PLATFORM_WORK_QUEUE_CALLBACK(platform_work_queue_callback);
    struct platform_work_queue_entry
    {
        platform_work_queue_callback *Callback;
        void *Data;
    };
    
    typedef void platform_add_entry(platform_work_queue *Queue, platform_work_queue_callback *CallBack, void *Data);
    typedef void platform_complete_all_work(platform_work_queue *Queue);
    //-
    
#define PLATFORM_CHANGE_CURSOR(name) void name(platform_cursor_shape Shape)
    typedef PLATFORM_CHANGE_CURSOR(platform_change_cursor);
    
    typedef struct game_memory
    {
        b32 IsInitialized;
        
        psize PermanentStorageSize;
        void *PermanentStorage; // NOTE(casey): REQUIRED to be cleared to zero at startup
        
        psize TransientStorageSize;
        void *TransientStorage; // NOTE(casey): REQUIRED to be cleared to zero at startup
        
        platform_work_queue *HighPriorityQueue;
        
        platform_add_entry *PlatformAddEntry;
        platform_complete_all_work *PlatformCompleteAllWork;
        platform_run_command_and_get_output *PlatformRunCommandAndGetOutput;
        platform_log *PlatformLog;
        platform_change_cursor *PlatformChangeCursor;
        platform_get_wall_clock *PlatformGetWallClock;
        
        debug_platform_free_file_memory *DEBUGPlatformFreeFileMemory;
        debug_platform_read_entire_file *DEBUGPlatformReadEntireFile;
        debug_platform_write_entire_file *DEBUGPlatformWriteEntireFile;
    } game_memory;
    
#define GAME_UPDATE_AND_RENDER(name) void name(thread_context *Thread, game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer)
    typedef GAME_UPDATE_AND_RENDER(game_update_and_render);
    
    // NOTE(casey): At the moment, this has to be a very fast function, it cannot be
    // more than a millisecond or so.
    // TODO(casey): Reduce the pressure on this function's performance by measuring it
    // or asking about it, etc.
#define GAME_GET_SOUND_SAMPLES(name) void name(thread_context *Thread, game_memory *Memory, game_sound_output_buffer *SoundBuffer)
    typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);
    
    inline u32
        SafeTruncateUInt64(u64 Value)
    {
        // TODO(casey): Defines for maximum values
        Assert(Value <= 0xFFFFFFFF);
        u32 Result = (u32)Value;
        return(Result);
    }
    
    inline game_controller_input *GetController(game_input *Input, u32 ControllerIndex)
    {
        Assert(ControllerIndex < ArrayCount(Input->Controllers));
        
        game_controller_input *Result = &Input->Controllers[ControllerIndex];
        return(Result);
    }
    
    global_variable platform_log *Log;
    
#ifdef __cplusplus
}
#endif

#endif //HANDMADE_PLATFORM_H
