#include <stdio.h>
//- POSIX  
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
//- GCC 
#include <x86gprintrin.h>
#include <xmmintrin.h>
//- Linux  
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <linux/input.h>
#include <alsa/asoundlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysymdef.h>
#include <X11/extensions/Xrandr.h>
#include <X11/cursorfont.h>
//- Handmade  
#include "handmade_platform.h"
#include "linux_handmade.h"
#include "linux_profile.h"
#include "linux_x11_keysyms_to_strings.c"

#define ALSA_RECOVER_SILENT true
#define MAX_PLAYER_COUNT 4

// NOTE(luca): Bits are layed out over multiple bytes.  This macro checks which byte the bit will be set in.
#define IsEvdevBitSet(Bit, Array) (Array[(Bit) / 8] & (1 << ((Bit) % 8)))
#define BytesNeededForBits(Bits) ((Bits + 7) / 8)

// TODO(luca): Use C11 atomics.

#define InterlockedIncrement(Pointer, Value) __sync_fetch_and_add(Pointer, Value)
// NOTE(luca): I used to use __sync_synchronize(), but that would add an extra
//`lock or` instruction.  (See Compiler Explorer).
#define ReadWriteBarrier __asm__ __volatile__ ("" : : : "memory")
#define InterlockedCompareExchange(Pointer, Exchange, Comparand) \
__sync_val_compare_and_swap(Pointer, Comparand, Exchange)

global_variable b32 GlobalRunning;
global_variable b32 GlobalPaused;

global_variable Window GlobalWindowHandle;
global_variable Display *GlobalDisplayHandle;

#define MemoryCopy memcpy
#define MemorySet memset

psize StringLength(char *String)
{
    psize Result = 0;
    
    while(*String++) Result++;
    
    return Result;
}

void CatStrings(size_t SourceACount, char *SourceA,
                size_t SourceBCount, char *SourceB,
                size_t DestCount, char *Dest)
{
    for(size_t Index = 0;
        Index < SourceACount;
        Index++)
    {
        *Dest++ = *SourceA++;
    }
    
    for(size_t Index = 0;
        Index < SourceBCount;
        Index++)
    {
        *Dest++ = *SourceB++;
    }
}

internal rune
ConvertUTF8StringToRune(u8 UTF8String[4])
{
    rune Codepoint = 0;
    
    if((UTF8String[0] & 0x80) == 0x00)
    {
        Codepoint = UTF8String[0];
    }
    else if((UTF8String[0] & 0xE0) == 0xC0)
    {
        Codepoint = (
                     ((UTF8String[0] & 0x1F) << 6*1) |
                     ((UTF8String[1] & 0x3F) << 6*0)
                     );
    }
    else if((UTF8String[0] & 0xF0) == 0xE0)
    {
        Codepoint = (
                     ((UTF8String[0] & 0x0F) << 6*2) |
                     ((UTF8String[1] & 0x3F) << 6*1) |
                     ((UTF8String[2] & 0x3F) << 6*0)
                     );
    }
    else if((UTF8String[0] & 0xF8) == 0xF8)
    {
        Codepoint = (
                     ((UTF8String[0] & 0x0E) << 6*3) |
                     ((UTF8String[1] & 0x3F) << 6*2) |
                     ((UTF8String[2] & 0x3F) << 6*1) |
                     ((UTF8String[3] & 0x3F) << 6*0)
                     );
    }
    else
    {
        Assert(0);
    }
    
    return Codepoint;
}

struct linux_init_alsa_result
{
    snd_pcm_t *PCMHandle;
    snd_pcm_hw_params_t *PCMParams;
};

internal linux_init_alsa_result LinuxInitALSA()
{
    linux_init_alsa_result Result = {};
    
    int PCMResult = 0;
    if((PCMResult = snd_pcm_open(&Result.PCMHandle, "default",
                                 SND_PCM_STREAM_PLAYBACK, 0)) == 0) 
    {
        snd_pcm_hw_params_alloca(&Result.PCMParams);
        snd_pcm_hw_params_any(Result.PCMHandle, Result.PCMParams);
        u32 ChannelCount = 2;
        u32 SampleRate = 48000;
        
        if((PCMResult = snd_pcm_hw_params_set_access(Result.PCMHandle, Result.PCMParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
        {
            // TODO(luca): Logging
            // snd_strerror(pcm)
        }
        if((PCMResult = snd_pcm_hw_params_set_format(Result.PCMHandle, Result.PCMParams, SND_PCM_FORMAT_S16_LE)) < 0)
        {
            // TODO(luca): Logging
        }
        if((PCMResult = snd_pcm_hw_params_set_channels(Result.PCMHandle, Result.PCMParams, ChannelCount)) < 0)
        {
            // TODO(luca): Logging
        }
        if((PCMResult = snd_pcm_hw_params_set_rate_near(Result.PCMHandle, Result.PCMParams, &SampleRate, 0)) < 0) 
        {
            // TODO(luca): Logging
        }    
        if((PCMResult = snd_pcm_nonblock(Result.PCMHandle, 1)) < 0)
        {
            // TODO(luca): Logging
        }
        if((PCMResult = snd_pcm_reset(Result.PCMHandle)) < 0)
        {
            // TODO(luca): Logging
        }
        if((PCMResult = snd_pcm_hw_params(Result.PCMHandle, Result.PCMParams)) < 0)
        {
            // TODO(luca): Logging
        }
        
    }
    else
    {
        // TODO(luca): Logging
    }
    
    return Result;
}

internal void
LinuxSigINTHandler(int SigNum)
{
    GlobalRunning = false;
}

//- Threading 
internal void 
LinuxAddEntry(platform_work_queue *Queue, platform_work_queue_callback *Callback, void *Data)
{
    // TODO(casey): Switch to InterlockedCompareExchange eventually so that any thread can add.
    u32 NewNextEntryToWrite = ((Queue->NextEntryToWrite + 1) % ArrayCount(Queue->Entries));
    Assert(NewNextEntryToWrite != Queue->NextEntryToRead);
    platform_work_queue_entry *Entry = Queue->Entries + Queue->NextEntryToWrite;
    Entry->Data = Data;
    Entry->Callback = Callback;
    Queue->CompletionGoal++;
    Queue->NextEntryToWrite = NewNextEntryToWrite;
    ReadWriteBarrier;
    s32 Ret = sem_post(&Queue->SemaphoreHandle);
    Assert(Ret == 0);
}

internal b32
LinuxDoNextWorkQueueEntry(platform_work_queue *Queue)
{
    b32 WeShouldSleep = false;
    
    u32 OriginalNextEntryToRead = Queue->NextEntryToRead;
    u32 NewNextEntryToRead = (OriginalNextEntryToRead + 1) % ArrayCount(Queue->Entries);
    if(Queue->NextEntryToRead != Queue->NextEntryToWrite)
    {
        u32 Index = InterlockedCompareExchange(&Queue->NextEntryToRead, 
                                               NewNextEntryToRead,
                                               OriginalNextEntryToRead);
        if(Index == OriginalNextEntryToRead)
        {
            platform_work_queue_entry Entry = Queue->Entries[Index];
            Entry.Callback(Queue, Entry.Data);
            InterlockedIncrement(&Queue->CompletionCount, 1);
        }
    }
    else
    {
        WeShouldSleep = true;
    }
    
    return WeShouldSleep;
}

internal void
LinuxCompleteAllWork(platform_work_queue *Queue)
{
    while((Queue->CompletionGoal != Queue->CompletionCount))
    {
        LinuxDoNextWorkQueueEntry(Queue);
    }
    
    Queue->CompletionGoal = 0;
    Queue->CompletionCount = 0;
}

void* ThreadProc(void *UserData)
{
    linux_thread_info *ThreadInfo = (linux_thread_info *)UserData;
    
    while(1)
    {
        if(LinuxDoNextWorkQueueEntry(ThreadInfo->Queue))
        {
            sem_wait(&ThreadInfo->Queue->SemaphoreHandle);
        }
    }
}

internal s64
LinuxGetThreadsCount(void)
{
    s64 Threads = sysconf(_SC_NPROCESSORS_CONF);
    return Threads;
}

//- Gamepads 
internal void 
LinuxGetAxisInfo(linux_gamepad *GamePad, linux_gamepad_axes_enum Axis, int AbsAxis)
{
    input_absinfo AxesInfo = {};
    if(ioctl(GamePad->File, EVIOCGABS(AbsAxis), &AxesInfo) != -1)
    {
        GamePad->Axes[Axis].Minimum = AxesInfo.minimum;
        GamePad->Axes[Axis].Maximum = AxesInfo.maximum;
        GamePad->Axes[Axis].Fuzz = AxesInfo.fuzz;
        GamePad->Axes[Axis].Flat = AxesInfo.flat;
    }
}

PLATFORM_WORK_QUEUE_CALLBACK(LinuxThreadCloseFD)
{
    s32 File = (s32)((u64)(Data));
    
    s32 Ret = close(File);
    if(Ret)
    {
        // TODO(luca): Logging
    }
}

internal void 
LinuxOpenGamePad(platform_work_queue *Queue, char *FilePath, linux_gamepad *GamePad)
{
    GamePad->File = open(FilePath, O_RDWR|O_NONBLOCK);
    
    if(GamePad->File != -1)
    {
        int Version = 0;
        int IsCompatible = true;
        
        // TODO(luca): Check versions
        ioctl(GamePad->File, EVIOCGVERSION, &Version);
        ioctl(GamePad->File, EVIOCGNAME(sizeof(GamePad->Name)), GamePad->Name);
        
        char SupportedEventBits[BytesNeededForBits(EV_MAX)] = {};
        if(ioctl(GamePad->File, EVIOCGBIT(0, sizeof(SupportedEventBits)), SupportedEventBits) != -1)
        {
            if(!IsEvdevBitSet(EV_ABS, SupportedEventBits))
            {
                // TODO(luca): Logging
                IsCompatible = false;
            }
            if(!IsEvdevBitSet(EV_KEY, SupportedEventBits))
            {
                // TODO(luca): Logging
                IsCompatible = false;
            }
        }
        
        char SupportedKeyBits[BytesNeededForBits(KEY_MAX)] = {};
        if(ioctl(GamePad->File, EVIOCGBIT(EV_KEY , sizeof(SupportedKeyBits)), SupportedKeyBits) != -1)
        {
            if(!IsEvdevBitSet(BTN_GAMEPAD, SupportedKeyBits))
            {
                // TODO(luca): Logging
                IsCompatible = false;
            }
        }
        
        GamePad->SupportsRumble = IsEvdevBitSet(EV_FF, SupportedEventBits);
        
        if(IsCompatible)
        {
            // NOTE(luca): Map evdev axes to my enum.
            LinuxGetAxisInfo(GamePad, LSTICKX, ABS_X);
            LinuxGetAxisInfo(GamePad, LSTICKY, ABS_Y);
            LinuxGetAxisInfo(GamePad, RSTICKX, ABS_RX);
            LinuxGetAxisInfo(GamePad, RSTICKY, ABS_RY);
            LinuxGetAxisInfo(GamePad, LSHOULDER, ABS_Z);
            LinuxGetAxisInfo(GamePad, RSHOULDER, ABS_RZ);
            LinuxGetAxisInfo(GamePad, DPADX, ABS_HAT0X);
            LinuxGetAxisInfo(GamePad, DPADY, ABS_HAT0Y);
            
            MemoryCopy(GamePad->FilePath, FilePath, StringLength(FilePath));
        }
        else
        {
            LinuxAddEntry(Queue, LinuxThreadCloseFD, (void *)GamePad->File);
            *GamePad = {};
            GamePad->File = -1;
        }
    }
}

// TODO(luca): Make this work in the case of multiple displays.
internal r32 LinuxGetMonitorRefreshRate(Display *DisplayHandle, Window RootWindow)
{
    r32 Result = 0;
    
    void *LibraryHandle = dlopen("libXrandr.so.2", RTLD_NOW);
    
    if(LibraryHandle)
    {
        typedef XRRScreenResources *xrr_get_screen_resources(Display *Display, Window Window);
        typedef XRRCrtcInfo *xrr_get_crtc_info(Display* Display, XRRScreenResources *Resources, RRCrtc Crtc);
        
        xrr_get_screen_resources *XRRGetScreenResources = (xrr_get_screen_resources *)dlsym(LibraryHandle, "XRRGetScreenResources");
        xrr_get_crtc_info *XRRGetCrtcInfo = (xrr_get_crtc_info *)dlsym(LibraryHandle, "XRRGetCrtcInfo");
        
        XRRScreenResources *ScreenResources = XRRGetScreenResources(DisplayHandle, RootWindow);
        
        RRMode ActiveModeID = 0;
        for(int CRTCIndex = 0;
            CRTCIndex< ScreenResources->ncrtc;
            CRTCIndex++)
        {
            XRRCrtcInfo *CRTCInfo = XRRGetCrtcInfo(DisplayHandle, ScreenResources, ScreenResources->crtcs[CRTCIndex]);
            if(CRTCInfo->mode)
            {
                ActiveModeID = CRTCInfo->mode;
            }
        }
        
        r32 ActiveRate = 0;
        for(int ModeIndex = 0;
            ModeIndex < ScreenResources->nmode;
            ModeIndex++)
        {
            XRRModeInfo ModeInfo = ScreenResources->modes[ModeIndex];
            if(ModeInfo.id == ActiveModeID)
            {
                Assert(ActiveRate == 0);
                Result = (r32)ModeInfo.dotClock / ((r32)ModeInfo.hTotal * (r32)ModeInfo.vTotal);
            }
        }
        
        dlclose(LibraryHandle);
    }
    
    return Result;
}

internal void LinuxBuildFileNameFromExecutable(char *Dest, linux_state *State, char *FileName)
{
    char *Path = State->ExecutablePath;
    
    size_t LastSlash = 0;
    for(char *Scan = Path;
        *Scan;
        Scan++)
    {
        if(*Scan == '/')
        {
            LastSlash = Scan - Path;
        }
    }
    
    for(size_t Index = 0;
        Index < LastSlash + 1;
        Index++)
    {
        *Dest++ = *Path++;
    }
    
    while(*FileName)
        *Dest++ = *FileName++;
}

GAME_UPDATE_AND_RENDER(LinuxGameUpdateAndRenderStub) {}
GAME_GET_SOUND_SAMPLES(LinuxGameGetSoundSamplesStub) {}

internal linux_game_code LinuxLoadGameCode(char *LibraryPath)
{
    linux_game_code Result = {};
    
    Result.LibraryHandle = dlopen(LibraryPath, RTLD_NOW);
    if(Result.LibraryHandle)
    {
        Result.UpdateAndRender = (game_update_and_render *)dlsym(Result.LibraryHandle, "GameUpdateAndRender");
        Result.GetSoundSamples = (game_get_sound_samples *)dlsym(Result.LibraryHandle, "GameGetSoundSamples");
    }
    else
    {
        printf("dlerror: %s\n", dlerror());
        Result.UpdateAndRender = (game_update_and_render *)LinuxGameUpdateAndRenderStub;
        Result.GetSoundSamples = (game_get_sound_samples *)LinuxGameGetSoundSamplesStub;
    }
    
    return Result;
}

internal void LinuxUnloadGameCode(linux_game_code *Game)
{
    if(Game->LibraryHandle)
    {
        dlclose(Game->LibraryHandle);
    }
}

internal void LinuxProcessKeyPress(game_button_state *ButtonState, b32 IsDown)
{
    if(ButtonState->EndedDown != IsDown)
    {
        ButtonState->EndedDown = IsDown;
        ButtonState->HalfTransitionCount++;
    }
}

internal inline
linux_replay_buffer *LinuxGetReplayBuffer(linux_state *State, int SlotIndex)
{
    linux_replay_buffer *Result = 0;
    
    Assert(SlotIndex < (int)ArrayCount(State->ReplayBuffers));
    Assert(SlotIndex >= 0);
    
    Result = &State->ReplayBuffers[SlotIndex];
    return Result;
}

internal inline
void LinuxAppendToReplayBuffer(linux_replay_buffer *Buffer, void *Memory, size_t Size)
{
    Assert(Buffer->MemorySize > Buffer->Pos + Size);
    
    MemoryCopy(Buffer->Memory + Buffer->Pos, Memory, Size);
    Buffer->Pos += Size;
}

internal 
void LinuxBeginRecordingInput(linux_state *State, int SlotIndex)
{
    linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, SlotIndex);
    
    if(Buffer->Memory == 0)
    {
        // NOTE(luca): Amount of frames of input to pre-allocate for. 
        int FramesCount = 2048;
        Buffer->MemorySize = State->TotalSize + 2048 * sizeof(game_input);
        Buffer->Memory = (char *)mmap(0, Buffer->MemorySize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|
                                      MAP_SHARED, -1, 0);
        if(Buffer->Memory == MAP_FAILED)
        {
            *Buffer = {};
        }
    }
    
    if(Buffer->Memory)
    {
        LinuxAppendToReplayBuffer(Buffer, State->GameMemoryBlock, State->TotalSize);
        State->InputRecordingIndex = SlotIndex;
    }
    
}

internal void LinuxEndRecordingInput(linux_state *State, int SlotIndex)
{
    Assert(State->InputRecordingIndex);
    
    linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, SlotIndex);
    Buffer->Size = Buffer->Pos;
    
    State->InputRecordingIndex = 0;
}

internal void 
LinuxBeginInputPlayBack(linux_state *State, int SlotIndex)
{
    linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, SlotIndex);
    
    MemoryCopy(State->GameMemoryBlock, Buffer->Memory, State->TotalSize);
    Buffer->Pos = State->TotalSize;
    
    State->InputPlayingIndex = SlotIndex;
}

internal void LinuxEndInputPlayBack(linux_state *State)
{
    Assert(State->InputPlayingIndex);
    
    linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, State->InputPlayingIndex);
}

internal void LinuxRecordInput(linux_state *State, game_input *Input)
{
    linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, State->InputRecordingIndex);
    
    LinuxAppendToReplayBuffer(Buffer, (char *)Input, sizeof(*Input));
}

internal void LinuxPlayBackInput(linux_state *State, game_input *Input)
{
    Assert(State->InputPlayingIndex);
    
    linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, State->InputPlayingIndex);
    MemoryCopy(Input, (Buffer->Memory + Buffer->Pos), sizeof(*Input));
    Buffer->Pos += sizeof(*Input);
    
    // NOTE(luca): This is where we do looping.
    if(Buffer->Pos >= Buffer->Size)
    {
        LinuxEndInputPlayBack(State);
        LinuxBeginInputPlayBack(State, State->InputPlayingIndex);
    }
}

internal void LinuxHideCursor(Display *DisplayHandle, Window WindowHandle)
{
    XColor black = {};
    char NoData[8] = {};
    
    Pixmap BitmapNoData = XCreateBitmapFromData(DisplayHandle, WindowHandle, NoData, 8, 8);
    Cursor InvisibleCursor = XCreatePixmapCursor(DisplayHandle, BitmapNoData, BitmapNoData, 
                                                 &black, &black, 0, 0);
    XDefineCursor(DisplayHandle, WindowHandle, InvisibleCursor);
    XFreeCursor(DisplayHandle, InvisibleCursor);
    XFreePixmap(DisplayHandle, BitmapNoData);
}

internal void LinuxShowCursor(Display *DisplayHandle, Window WindowHandle)
{
    XUndefineCursor(DisplayHandle, WindowHandle);
}

internal void LinuxSetSizeHint(Display *DisplayHandle, Window WindowHandle,
                               int MinWidth, int MinHeight,
                               int MaxWidth, int MaxHeight)
{
    XSizeHints Hints = {};
    if(MinWidth > 0 && MinHeight > 0) Hints.flags |= PMinSize;
    if(MaxWidth > 0 && MaxHeight > 0) Hints.flags |= PMaxSize;
    
    Hints.min_width = MinWidth;
    Hints.min_height = MinHeight;
    Hints.max_width = MaxWidth;
    Hints.max_height = MaxHeight;
    
    XSetWMNormalHints(DisplayHandle, WindowHandle, &Hints);
}

internal void LinuxProcessPendingMessages(Display *DisplayHandle, Window WindowHandle,
                                          XIC InputContext, Atom WM_DELETE_WINDOW, linux_state *State, game_input *Input, game_controller_input *KeyboardController)
{
    XEvent WindowEvent = {};
    while(XPending(DisplayHandle) > 0)
    {
        XNextEvent(DisplayHandle, &WindowEvent);
        b32 FilteredEvent = XFilterEvent(&WindowEvent, 0);
        if(FilteredEvent)
        {
            Assert(WindowEvent.type == KeyPress || WindowEvent.type == KeyRelease);
        }
        
        switch(WindowEvent.type)
        {
            case KeyPress:
            case KeyRelease:
            {
                //- How text input works 
                // The needs:
                //  1. Preserve game buttons, so that we can switch between a "game mode" or 
                //     "text input mode".
                //  2. Text input using the input method of the user which should allow for utf8 characters.
                //  3. Hotkey support.  Eg. quickly navigating text.
                // 3 will be supported by 2 for code reuse.
                //
                // We are going to send a buffer text button presses to the game layer, this solves these
                // issues:
                // - Pressing the same key multiple times in one frame.
                // - Having modifiers be specific to each key press.
                // - Not having to create a button record for each possible character in the structure.
                // - Key events come in one at a time in the event loop, thus we need to have a buffer for
                //   multiple keys pressed on a single frame.
                //
                // We store a count along the buffer and in the buffer we store the utf8 codepoint and its
                // modifiers.
                // The app code is responsible for traversing this buffer and applying the logic. 
                
                // The problem of input methods and hotkeys: 
                // Basically the problem is that if we allow the input method and combo's that could be 
                // filtered by the input method it won't seem consistent to the user.
                // So we don't allow key bound to the input method to have an effect and we only pass key
                // inputs that have not been filtered.
                //
                // In the platform layer we handle the special case were the input methods creates non-
                // printable characters and we decompose those key inputs since non-printable characters
                // have no use anymore.
                
                // Extra:
                // - I refuse to check which keys bind to what modifiers. It's not important.
                
                // - Handy resources: 
                //   - https://www.coderstool.com/unicode-text-converter
                //   - man Compose(5).
                //   - https://en.wikipedia.org/wiki/Control_key#History
                
                KeySym Symbol = XLookupKeysym(&WindowEvent.xkey, 0);
                b32 IsDown = (WindowEvent.type == KeyPress);
                
                // TODO(luca): Refresh mappings.
                // NOTE(luca): Only KeyPress events  see man page of Xutf8LookupString().  And skip filtered events for text input, but keep them for controller.
                if(IsDown && !FilteredEvent)
                {
                    // TODO(luca): Choose a better error value.
                    rune Codepoint = L'ù';
                    u8 LookupBuffer[4] = {};
                    Status LookupStatus = {};
                    
                    s32 BytesLookepdUp = Xutf8LookupString(InputContext, &WindowEvent.xkey, 
                                                           (char *)&LookupBuffer, ArrayCount(LookupBuffer), 
                                                           0, &LookupStatus);
                    Assert(LookupStatus != XBufferOverflow);
                    Assert(BytesLookepdUp <= 4);
                    
                    if(LookupStatus!= XLookupNone &&
                       LookupStatus!= XLookupKeySym)
                    {
                        if(BytesLookepdUp)
                        {
                            Assert(KeyboardController->Text.Count < ArrayCount(KeyboardController->Text.Buffer));
                            
                            Codepoint = ConvertUTF8StringToRune(LookupBuffer);
                            
                            // NOTE(luca): Input methods might produce non printable characters (< ' ').  If this
                            // happens we try to "decompose" the key input.
                            if(Codepoint < ' ' && Codepoint >= 0)
                            {
                                if(Symbol >= XK_space)
                                {
                                    Codepoint = (char)(' ' + (Symbol - XK_space));
                                }
                            }
                            
                            // NOTE(luca): Since this is only for text input we pass Return and Backspace as codepoints.
                            if((Codepoint >= ' ' || Codepoint < 0) ||
                               Codepoint == '\r' || Codepoint == '\b' || Codepoint == '\n')
                            {                            
                                game_text_button *TextButton = &KeyboardController->Text.Buffer[KeyboardController->Text.Count++];
                                TextButton->Codepoint = Codepoint;
                                TextButton->Shift   = (WindowEvent.xkey.state & ShiftMask);
                                TextButton->Control = (WindowEvent.xkey.state & ControlMask);
                                TextButton->Alt     = (WindowEvent.xkey.state & Mod1Mask);
#if 0                           
                                printf("%d bytes '%c' %d (%c|%c|%c)\n", 
                                       BytesLookepdUp, 
                                       ((Codepoint >= ' ') ? (char)Codepoint : '\0'),
                                       Codepoint,
                                       ((WindowEvent.xkey.state & ShiftMask)   ? 'S' : ' '),
                                       ((WindowEvent.xkey.state & ControlMask) ? 'C' : ' '),
                                       ((WindowEvent.xkey.state & Mod1Mask)    ? 'A' : ' '));
#endif
                            }
                            else
                            {
                                // TODO(luca): Logging
                            }
                            
                        }
                    }
                }
                
                if(0) {}
                else if(Symbol == XK_w)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveUp, IsDown);
                }
                else if(Symbol == XK_a)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveLeft, IsDown);
                }
                else if(Symbol == XK_r)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveDown, IsDown);
                }
                else if(Symbol == XK_s)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveRight, IsDown);
                }
                else if(Symbol == XK_Up)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionUp, IsDown);
                }
                else if(Symbol == XK_Left)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionLeft, IsDown);
                }
                else if(Symbol == XK_Down)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionDown, IsDown);
                }
                else if(Symbol == XK_Right)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionRight, IsDown);
                }
                else if(Symbol == XK_space)
                {
                    LinuxProcessKeyPress(&KeyboardController->Start, IsDown);
                }
                else if((WindowEvent.xkey.state & Mod1Mask) &&
                        (Symbol == XK_p))
                {
                    if(IsDown)
                    {
                        GlobalPaused = !GlobalPaused;
                    }
                }
                else if((WindowEvent.xkey.state & Mod1Mask) &&
                        (Symbol == XK_l))
                {
                    if(IsDown)
                    {
                        if(State->InputPlayingIndex == 0)
                        {
                            if(State->InputRecordingIndex == 0)
                            {
                                PROFILE_START_LABEL("begin recording");
                                LinuxBeginRecordingInput(State, 1);
                                PROFILE_END;
                            }
                            else
                            {
                                PROFILE_START_LABEL("end recording");
                                LinuxEndRecordingInput(State, 1);
                                PROFILE_END;
                                PROFILE_START_LABEL("begin playback");
                                LinuxBeginInputPlayBack(State, 1);
                                PROFILE_END;
                            }
                        }
                        else
                        {
                            // TODO(luca): Reset buttons so they aren't held?
                            for(u32 ButtonIndex = 0;
                                ButtonIndex < ArrayCount(KeyboardController->Buttons);
                                ButtonIndex++)
                            {
                                KeyboardController->Buttons[ButtonIndex] = {};
                            }
                            PROFILE_START_LABEL("end playback");
                            LinuxEndInputPlayBack(State);
                            PROFILE_END;
                            // TODO(luca): Save to a file and free
                            linux_replay_buffer *Buffer = LinuxGetReplayBuffer(State, State->InputPlayingIndex);
                            
                            int Res = munmap(Buffer->Memory, Buffer->MemorySize);
                            if(Res == 0)
                            {
                                *Buffer = {};
                            }
                            else
                            {
                                Buffer->Pos = 0;
                                Buffer->Size = 0;
                            }
                            State->InputPlayingIndex = 0;
                            
                        }
                    }
                }
                else if((WindowEvent.xkey.state & Mod1Mask) &&
                        (Symbol == XK_Return))
                {
                    if(IsDown)
                    {
                        // NOTE(luca): Something like EWMH hints exists, which could be used to set the window to fullscreen instead: https://www.tonyobryan.com//index.php?article=9
                        // TODO(luca): Move to LinuxState
                        
                        // Notify the window manager that we want to remove window decorations.
                        struct hints
                        {
                            unsigned long   flags;
                            unsigned long   functions;
                            unsigned long   decorations;
                            long            inputMode;
                            unsigned long   status;
                        };
                        
                        s32 XRet = 0;
                        s32 Width, Height, X, Y;
                        hints Hints = {};
                        Atom property = XInternAtom(DisplayHandle, "_MOTIF_WM_HINTS", True);
                        
                        // TODO(luca): Pass in actual width/height of window and max resolution of screen and modify these values accordingly.
                        if(!State->IsFullScreen)
                        {
                            Hints.flags = 2;
                            Hints.decorations = false;
                            
                            Width = 1920;
                            Height = 1080;
                            X = 0;
                            Y = 0;
                            
                            /*
                                                        // Grab mouse and keyboard input.
                            XRet = XMapRaised(DisplayHandle, WindowHandle); // Sometimes works.
                                                        XRet = XGrabPointer(DisplayHandle, WindowHandle, true, 0, GrabModeAsync, GrabModeAsync, WindowHandle, 0L, CurrentTime);
                                                        XRet = XGrabKeyboard(DisplayHandle, WindowHandle, false, GrabModeAsync, GrabModeAsync, CurrentTime);
                                                        */
                            
                            LinuxSetSizeHint(DisplayHandle, WindowHandle, Width, Height, Width, Height);
                        }
                        else
                        {
                            Hints.flags = 2;
                            Hints.decorations = true;
                            
                            Width = 1920/2;
                            Height = 1080/2;
                            X = 1920 - Width - 10;
                            Y = 10;
                            
                            XRet = XUngrabKeyboard(DisplayHandle, CurrentTime);
                            XRet = XUngrabPointer(DisplayHandle, CurrentTime);
                            LinuxSetSizeHint(DisplayHandle, WindowHandle, 0, 0, 0, 0);
                        }
                        
                        XRet = XChangeProperty(DisplayHandle, WindowHandle, 
                                               property, property, 
                                               32, PropModeReplace, (unsigned char*)&Hints, 5);
                        XRet = XMoveResizeWindow(DisplayHandle, WindowHandle, X, Y, Width, Height);
                        
                        State->IsFullScreen = !State->IsFullScreen;
                    }
                }
                else if((WindowEvent.xkey.state & Mod1Mask) && 
                        (Symbol == XK_F4))
                {
                    GlobalRunning = false;
                }
            } break;
            
            case ButtonPress:
            case ButtonRelease:
            {
                b32 IsDown = (WindowEvent.type == ButtonPress);
                u32 Button = WindowEvent.xbutton.button;
                
                if(0) {}
                else if(Button == Button1)
                {
                    LinuxProcessKeyPress(&Input->MouseButtons[PlatformMouseButton_Left], IsDown);
                }
                else if(Button == Button2)
                {
                    LinuxProcessKeyPress(&Input->MouseButtons[PlatformMouseButton_Middle], IsDown);
                }
                else if(Button == Button3)
                {
                    LinuxProcessKeyPress(&Input->MouseButtons[PlatformMouseButton_Right], IsDown);
                }
                else if(Button == Button4)
                {
                    LinuxProcessKeyPress(&Input->MouseButtons[PlatformMouseButton_ScrollUp], IsDown);
                }
                else if(Button == Button5)
                {
                    LinuxProcessKeyPress(&Input->MouseButtons[PlatformMouseButton_ScrollDown], IsDown);
                }
            } break;
            
            case DestroyNotify:
            {
                XDestroyWindowEvent *Event = (XDestroyWindowEvent *)&WindowEvent;
                if(Event->window == WindowHandle)
                {
                    GlobalRunning = false;
                }
            } break;
            
            case ClientMessage:
            {
                XClientMessageEvent *Event = (XClientMessageEvent *)&WindowEvent;
                if((Atom)Event->data.l[0] == WM_DELETE_WINDOW)
                {
                    XDestroyWindow(DisplayHandle, WindowHandle);
                    GlobalRunning = false;
                }
            } break;
            
            case EnterNotify:
            {
                //LinuxHideCursor(DisplayHandle, WindowHandle);
            } break;
            
            case LeaveNotify:
            {
                //LinuxShowCursor(DisplayHandle, WindowHandle);
            } break;
            
            case ConfigureNotify:
            {
                XConfigureEvent Event = WindowEvent.xconfigure;
                
                b32 SizeDidNotChange = ((!State->IsFullScreen && (Event.width == 1920/2 && Event.height == 1080/2)) ||
                                        (State->IsFullScreen && (Event.width == 1920 && Event.height == 1080)));
                b32 ChangedToFS = ((State->IsFullScreen) && (Event.x == 0 && Event.y == 0));
                
                // TODO(luca): Only do this on resize.
#if 0                
                if(!SizeDidNotChange || ChangedToFS)
                {
                    GC GraphicContext = XCreateGC(DisplayHandle, WindowHandle, GCForeground, &(XGCValues){.foreground = 0xFF00FF});
                    s32 XRet = XFillRectangle(DisplayHandle, WindowHandle, 
                                              GraphicContext, 0, 0, 1920, 1080);
                }
#endif
                
            } break;
            
        }
    }
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile)
{
    debug_platform_read_file_result Result = {};
    
    int File = open(FileName, O_RDONLY);
    if(File != -1)
    {
        struct stat FileStats = {};
        fstat(File, &FileStats);
        Result.ContentsSize = FileStats.st_size;
        Result.Contents = mmap(0, FileStats.st_size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE, File, 0);
        
        close(File);
    }
    
    return Result;
}

PLATFORM_LOG(PlatformLog)
{
    printf("%s\n", Text);
}

PLATFORM_RUN_COMMAND_AND_GET_OUTPUT(PlatformRunCommandAndGetOutput)
{
    psize Result = 0;
    
    int HandlesLink[2] = {0};
    int WaitStatus = 0;
    pid_t ChildPID = 0;
    int Ret = 0;
    
    char *FilePath = Command[0];
    int AccessMode = F_OK | X_OK;
    Ret = access(FilePath, AccessMode);
    
    if(Ret == 0)
    {
        Ret = pipe(HandlesLink);
        if(Ret != -1)
        {
            ChildPID = fork();
            if(ChildPID != -1)
            {
                if(ChildPID == 0)
                {
                    dup2(HandlesLink[1], STDOUT_FILENO);
                    execve(Command[0], Command, 0);
                }
                else
                {
                    wait(&WaitStatus);
                    
                    Result = read(HandlesLink[0], OutputBuffer, 4096);
                    if(Result == -1)
                    {
                        Result = 0;
                    }
                }
                
            }
            else
            {
                // TODO: Logging
            }
        }
        else
        {
            // TODO: Logging
        }
    }
    else
    {
        // TODO: Logging
    }
    
    return Result;
}

PLATFORM_CHANGE_CURSOR(PlatformChangeCursor)
{
    unsigned int XShape;
    
    switch(Shape)
    {
        case PlatformCursorShape_None:
        {
        } break;;
        case PlatformCursorShape_Grab:
        {
            XShape = XC_hand2;
        } break;
    }
    
    if(Shape != PlatformCursorShape_None)
    {
        Cursor FontCursor = XCreateFontCursor(GlobalDisplayHandle, XShape);
        XDefineCursor(GlobalDisplayHandle, GlobalWindowHandle, FontCursor);
    }
    else
    {
        XUndefineCursor(GlobalDisplayHandle, GlobalWindowHandle);
    }
    
}

DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory)
{
    munmap(Memory, MemorySize);
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
{
    b32 Result = false;
    
    int FD = open(FileName, O_CREAT|O_WRONLY|O_TRUNC, 00600);
    if(FD != -1)
    {
        if(write(FD, Memory, MemorySize) != MemorySize)
        {
            Result = true;
        }
        
        close(FD);
    }
    
    return Result;
}

internal struct timespec LinuxGetLastWriteTime(char *FilePath)
{
    struct timespec Result = {};
    
    struct stat LibraryFileStats = {};
    if(!stat(FilePath, &LibraryFileStats))
    {
        Result = LibraryFileStats.st_mtim;
    }
    
    return Result;
}

internal struct timespec LinuxGetWallClock()
{
    struct timespec Counter = {};
    clock_gettime(CLOCK_MONOTONIC, &Counter);
    return Counter;
}

internal s64 LinuxGetNSecondsElapsed(struct timespec Start, struct timespec End)
{
    s64 Result = 0;
    Result = ((s64)End.tv_sec*1000000000 + (s64)End.tv_nsec) - ((s64)Start.tv_sec*1000000000 + (s64)Start.tv_nsec);
    return Result;
}

internal r32 LinuxGetSecondsElapsed(struct timespec Start, struct timespec End)
{
    r32 Result = 0;
    Result = ((r32)LinuxGetNSecondsElapsed(Start, End)/1000.0f/1000.0f/1000.0f);
    
    return Result;
}

internal r32 LinuxNormalizeAxisValue(s32 Value, linux_gamepad_axis Axis)
{
    r32 Result = 0;
    if(Value)
    {
        // ((value - min / max) - 0.5) * 2  
        r32 Normalized = ((r32)((r32)(Value - Axis.Minimum) / (r32)(Axis.Maximum - Axis.Minimum)) - 0.5f)*2;
        Result = Normalized;
    }
    Assert(Result <= 1.0f && Result >= -1.0f);
    
    return Result;
}

void LinuxDebugVerticalLine(game_offscreen_buffer *Buffer, int X, int Y, u32 Color)
{
    int Height = 32;
    
    if(X <= Buffer->Width && X >= 0 &&
       Y <= Buffer->Height - Height && Y <= 0)
    {
        u8 *Row = (u8 *)Buffer->Memory + Y*Buffer->Pitch + X*Buffer->BytesPerPixel;
        while(Height--)
        {
            *(u32 *)Row = Color;
            Row += Buffer->Pitch;
        }
    }
}

internal void
LinuxChangeToExecutableDirectory(char *Args[])
{
    char *ExePath = Args[0];
    s32 Length = (s32)StringLength(ExePath);
    char ExecutableDirPath[Length];
    s32 LastSlash = 0;
    for(s32 At = 0;
        At < Length;
        At++)
    {
        if(ExePath[At] == '/')
        {
            LastSlash = At;
        }
    }
    MemoryCopy(ExecutableDirPath, ExePath, LastSlash);
    ExecutableDirPath[LastSlash] = 0;
    if(chdir(ExecutableDirPath) == -1)
    {
        // TODO(luca): Logging
    }
}

//~ Main
int 
main(int ArgC, char *Args[])
{
    
    GlobalRunning = true;
    signal(SIGINT, LinuxSigINTHandler);
    
    linux_state LinuxState = {};
    
    // NOTE(luca): Change to executable's current directory so that all file paths can be
    // consistent and relative.
    LinuxChangeToExecutableDirectory(Args);
    
    u32 ThreadCount = (u32)LinuxGetThreadsCount()/2 - 1;
    u32 InitialCount = 0;
    s32 Ret = 0;
    
    platform_work_queue Queue = {};
    
    Ret = sem_init(&Queue.SemaphoreHandle, 0, InitialCount);
    Assert(Ret == 0);
    
    linux_thread_info ThreadInfo[ThreadCount - 1];
    for(u32 ThreadIndex = 0;
        ThreadIndex < ArrayCount(ThreadInfo);
        ThreadIndex++)
    {
        linux_thread_info *Info = &ThreadInfo[ThreadIndex];
        *Info = {};
        Info->Queue = &Queue;
        Info->LogicalThreadIndex = ThreadIndex;
        pthread_t ThreadHandle = 0;
        s32 Ret = pthread_create(&ThreadHandle, 0, ThreadProc, Info);
        if(Ret != 0)
        {
            // TODO(luca): Logging.
        }
    }
    
    //- X 
    s32 XRet = 0;
    Display *DisplayHandle = XOpenDisplay(0);
    
    if(DisplayHandle)
    {
        GlobalDisplayHandle = DisplayHandle;
        Window RootWindow = XDefaultRootWindow(DisplayHandle);
        s32 Screen = XDefaultScreen(DisplayHandle);
#if HANDMADE_SMALL_RESOLUTION
        s32 Width = 1920/2;
        s32 Height = 1080/2;
#else
        s32 Width = 1920;
        s32 Height = 1080;
#endif
        s32 ScreenBitDepth = 24;
        XVisualInfo WindowVisualInfo = {};
        if(XMatchVisualInfo(DisplayHandle, Screen, ScreenBitDepth, TrueColor, &WindowVisualInfo))
        {
            XSetWindowAttributes WindowAttributes = {};
            WindowAttributes.bit_gravity = StaticGravity;
#if HANDMADE_INTERNAL            
            WindowAttributes.background_pixel = 0xFF00FF;
#endif
            WindowAttributes.colormap = XCreateColormap(DisplayHandle, RootWindow, WindowVisualInfo.visual, AllocNone);
            WindowAttributes.event_mask = (StructureNotifyMask | 
                                           KeyPressMask | KeyReleaseMask | 
                                           ButtonPressMask | ButtonReleaseMask |
                                           EnterWindowMask | LeaveWindowMask);
            u64 WindowAttributeMask = CWBitGravity | CWBackPixel | CWColormap | CWEventMask;
            
            Window WindowHandle = XCreateWindow(DisplayHandle, RootWindow,
                                                1920 - Width - 10, 10,
                                                Width, Height,
                                                0,
                                                WindowVisualInfo.depth, InputOutput,
                                                WindowVisualInfo.visual, WindowAttributeMask, &WindowAttributes);
            if(WindowHandle)
            {
                GlobalWindowHandle = WindowHandle;
                XRet = XStoreName(DisplayHandle, WindowHandle, "Handmade Window");
                
                // NOTE(luca): If we set the MaxWidth and MaxHeigth to the same values as MinWidth and MinHeight there is a bug on dwm where it won't update the window decorations when trying to remove them.
                // In the future we will allow resizing to any size so this does not matter that much.
                
                LinuxSetSizeHint(DisplayHandle, WindowHandle, 0, 0, 0, 0);
                
                // NOTE(luca): Tiling window managers should treat windows with the WM_TRANSIENT_FOR property as pop-up windows.  This way we ensure that we will be a floating window.  This works on my setup (dwm). 
                XRet = XSetTransientForHint(DisplayHandle, WindowHandle, RootWindow);
                
                Atom WM_DELETE_WINDOW = XInternAtom(DisplayHandle, "WM_DELETE_WINDOW", False);
                if(!XSetWMProtocols(DisplayHandle, WindowHandle, &WM_DELETE_WINDOW, 1))
                {
                    // TODO(luca): Logging
                }
                
                XClassHint ClassHint = {};
                ClassHint.res_name = "Handmade Window";
                ClassHint.res_class = "Handmade Window";
                XSetClassHint(DisplayHandle, WindowHandle, &ClassHint);
                
                XSetLocaleModifiers("");
                
                XIM InputMethod = XOpenIM(DisplayHandle, 0, 0, 0);
                if(!InputMethod){
                    XSetLocaleModifiers("@im=none");
                    InputMethod = XOpenIM(DisplayHandle, 0, 0, 0);
                }
                XIC InputContext = XCreateIC(InputMethod,
                                             XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                             XNClientWindow, WindowHandle,
                                             XNFocusWindow,  WindowHandle,
                                             NULL);
                XSetICFocus(InputContext);
                
                int BitsPerPixel = 32;
                int BytesPerPixel = BitsPerPixel/8;
                int WindowBufferSize = Width*Height*BytesPerPixel;
                // TODO(luca): Get rid of this malloc!!!
                char *WindowBuffer = (char *)malloc(WindowBufferSize);
                
                XImage *WindowImage = XCreateImage(DisplayHandle, WindowVisualInfo.visual, WindowVisualInfo.depth, ZPixmap, 0, WindowBuffer, Width, Height, BitsPerPixel, 0);
                GC DefaultGC = DefaultGC(DisplayHandle, Screen);
                
                //- Game memory 
                
                linux_state LinuxState = {};
                MemoryCopy(LinuxState.ExecutablePath, Args[0], StringLength(Args[0]));
                
                char LibraryFullPath[] = "./handmade.so";
                linux_game_code Game = LinuxLoadGameCode(LibraryFullPath);
                Game.LibraryLastWriteTime = LinuxGetLastWriteTime(LibraryFullPath);
                
                thread_context ThreadContext = {};
                
                game_memory GameMemory = {};
                GameMemory.PermanentStorageSize = Megabytes(64);
                GameMemory.TransientStorageSize = Gigabytes(1);
                GameMemory.HighPriorityQueue = &Queue;
                GameMemory.PlatformAddEntry = LinuxAddEntry;
                GameMemory.PlatformCompleteAllWork = LinuxCompleteAllWork;
                GameMemory.PlatformLog = PlatformLog;
                GameMemory.PlatformChangeCursor = PlatformChangeCursor;
                GameMemory.PlatformRunCommandAndGetOutput = PlatformRunCommandAndGetOutput;
                GameMemory.PlatformGetWallClock = LinuxGetWallClock;
                GameMemory.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
                GameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;
                GameMemory.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
                
#if HANDMADE_INTERNAL
                void *BaseAddress = (void *)Terabytes(2);
#else
                void *BaseAddress = 0;
#endif
                // TODO(casey): TransientStorage needs to be broken into game transient and cache transient
                LinuxState.TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;
                LinuxState.GameMemoryBlock = mmap(BaseAddress, LinuxState.TotalSize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
                GameMemory.PermanentStorage = LinuxState.GameMemoryBlock;
                GameMemory.TransientStorage = (u8 *)GameMemory.PermanentStorage + GameMemory.PermanentStorageSize;
                
                game_input Input[2] = {};
                game_input *NewInput = &Input[0];
                game_input *OldInput = &Input[1];
                
                linux_gamepad GamePads[MAX_PLAYER_COUNT] = {};
                for(int GamePadIndex = 0;
                    GamePadIndex < MAX_PLAYER_COUNT;
                    GamePadIndex++)
                {
                    GamePads[GamePadIndex].File = -1;
                }
                
                char EventDirectoryName[] = "/dev/input/";
                
                // TODO(luca): Use homemade instaed of libc.
                DIR *EventDirectory = opendir(EventDirectoryName);
                struct dirent *Entry = 0;
                int GamePadIndex = 0;
                while((Entry = readdir(EventDirectory)))
                {
                    if(!strncmp(Entry->d_name, "event", sizeof("event") - 1))
                    {
                        char FilePath[PATH_MAX] = {};
                        CatStrings(sizeof(EventDirectoryName) - 1, EventDirectoryName,
                                   StringLength(Entry->d_name), Entry->d_name,
                                   sizeof(FilePath) - 1, FilePath);
                        if(GamePadIndex < MAX_PLAYER_COUNT)
                        {
                            linux_gamepad *GamePadAt = &GamePads[GamePadIndex];
                            LinuxOpenGamePad(&Queue, FilePath, GamePadAt);
                            if(GamePadAt->File != -1)
                            {
#if 0
                                for(int AxesIndex = 0;
                                    AxesIndex < AXES_COUNT;
                                    AxesIndex++)
                                {
                                    linux_gamepad_axis Axis = GamePadAt->Axes[AxesIndex];
                                    printf("min: %d, max: %d, fuzz: %d, flat: %d\n", Axis.Minimum, Axis.Maximum, Axis.Fuzz, Axis.Flat);
                                }
#endif
                                GamePadIndex++;
                            }
                        }
                        else
                        {
                            // TODO(luca): Logging
                        }
                    }
                }
                
                LinuxCompleteAllWork(&Queue);
                
                game_offscreen_buffer OffscreenBuffer = {};
                OffscreenBuffer.Memory = WindowBuffer;
                OffscreenBuffer.Width = Width;
                OffscreenBuffer.Height = Height;
                OffscreenBuffer.BytesPerPixel = BytesPerPixel;
                OffscreenBuffer.Pitch = Width*BytesPerPixel;
                
                int LastFramesWritten = 0;
                unsigned int SampleRate, ChannelCount, PeriodTime, SampleCount;
                snd_pcm_status_t *PCMStatus = 0;
                snd_pcm_t* PCMHandle = 0;
                snd_pcm_hw_params_t *PCMParams = 0;
                snd_pcm_uframes_t PeriodSize = 0;
                snd_pcm_uframes_t PCMBufferSize = 0;
                
                linux_init_alsa_result ALSAInit = LinuxInitALSA();
                PCMHandle = ALSAInit.PCMHandle;
                PCMParams = ALSAInit.PCMParams;
                snd_pcm_hw_params_get_channels(PCMParams, &ChannelCount);
                snd_pcm_hw_params_get_rate(PCMParams, &SampleRate, 0);
                snd_pcm_hw_params_get_period_size(PCMParams, &PeriodSize, 0);
                snd_pcm_hw_params_get_period_time(PCMParams, &PeriodTime, NULL);
                snd_pcm_hw_params_get_buffer_size(PCMParams, &PCMBufferSize);
                snd_pcm_status_malloc(&PCMStatus);
                
#if 0
                {
                    Assert(0);
                    u32 Value, Result;
                    snd_pcm_uframes_t Frames;
                    Result = snd_pcm_hw_params_get_buffer_time_min(PCMParams, &Value, 0);
                    Result = snd_pcm_hw_params_get_buffer_size_min(PCMParams, &Frames);
                    Frames = PCMBufferSize/2;
                    Result = snd_pcm_hw_params_set_period_size_near(PCMHandle, PCMParams, &Frames, 0);
                    Result = snd_pcm_hw_params_get_period_size(PCMParams, &PeriodSize, 0);
                }
#endif
                
                char AudioSamples[PCMBufferSize];
                u64 Periods = 2;
                u32 BytesPerSample = (sizeof(s16)*ChannelCount);
                
#if HANDMADE_FORCE_UPDATEHZ
                r32 GameUpdateHz = HANDMADE_FORCE_UPDATEHZ;
#elif 1
                r32 GameUpdateHz = 30;
#else
                r32 GameUpdateHz = LinuxGetMonitorRefreshRate(DisplayHandle, RootWindow);
#endif
                
#if HANDMADE_PROFILING
                if(!spall_init_file("linux_handmade.spall", 1, &spall_ctx)) 
                {
                    printf("Failed to setup spall?\n");
                }
                u8 SpallBackingBuffer[1 * 1024 * 1024] = {};
                spall_buffer.data = SpallBackingBuffer;
                spall_buffer.length = sizeof(SpallBackingBuffer) - 1;
                if(!spall_buffer_init(&spall_ctx, &spall_buffer))
                {
                    printf("Failed to init spall buffer?\n");
                    return 1;
                }
                PlatformGetWallClock = LinuxGetWallClock;
#endif
                
                
                XRet = XMapWindow(DisplayHandle, WindowHandle);
                XRet = XFlush(DisplayHandle);
                
                struct timespec LastCounter = LinuxGetWallClock();
                struct timespec FlipWallClock = LinuxGetWallClock();
                r32 TargetSecondsPerFrame = 1.0f / GameUpdateHz; 
                
                u64 LastCycleCount = __rdtsc();
                while(GlobalRunning)
                {
                    PROFILE_START_LABEL("Inputs");
                    NewInput->dtForFrame = TargetSecondsPerFrame;
                    
#if HANDMADE_INTERNAL
                    // NOTE(luca): Because gcc will first create an empty file and then write into it we skip trying to reload when the file is empty.
                    struct stat FileStats = {};
                    stat(LibraryFullPath, &FileStats);
                    if(FileStats.st_size)
                    {
                        s64 SecondsElapsed = LinuxGetNSecondsElapsed(Game.LibraryLastWriteTime, FileStats.st_mtim) / 1000/1000;
                        if(SecondsElapsed > 0)
                        {
                            LinuxUnloadGameCode(&Game);
                            Game = LinuxLoadGameCode(LibraryFullPath);
                            Game.LibraryLastWriteTime = FileStats.st_mtim;
                        }
                    }
#endif
                    
                    game_controller_input *OldKeyboardController = GetController(OldInput, 0);
                    game_controller_input *NewKeyboardController = GetController(NewInput, 0);
                    NewKeyboardController->IsConnected = true;
                    
                    NewKeyboardController->Text.Count = 0;
                    for(u32 ButtonIndex = 0;
                        ButtonIndex < ArrayCount(NewKeyboardController->Buttons);
                        ButtonIndex++)
                    {
                        NewKeyboardController->Buttons[ButtonIndex].HalfTransitionCount = 0;
                    }
                    for(u32 ButtonIndex = 0;
                        ButtonIndex < PlatformMouseButton_Count;
                        ButtonIndex++)
                    {
                        NewInput->MouseButtons[ButtonIndex].EndedDown = OldInput->MouseButtons[ButtonIndex].EndedDown;
                        NewInput->MouseButtons[ButtonIndex].HalfTransitionCount = 0;
                    }
                    
                    LinuxProcessPendingMessages(DisplayHandle, WindowHandle, InputContext, WM_DELETE_WINDOW,
                                                &LinuxState, NewInput, NewKeyboardController);
                    
                    // TODO(luca): Use MotionNotify events instead so we query this less frequently.
                    s32 MouseX = 0, MouseY = 0, MouseZ = 0;
                    u32 MouseMask = 0;
                    u64 Ignored;
                    if(XQueryPointer(DisplayHandle, WindowHandle, 
                                     &Ignored, &Ignored, (int *)&Ignored, (int *)&Ignored,
                                     &MouseX, &MouseY, &MouseMask))
                    {
                        if(MouseX <= OffscreenBuffer.Width &&
                           MouseX >= 0 &&
                           MouseY <= OffscreenBuffer.Height &&
                           MouseY >= 0)
                        {
                            NewInput->MouseY = MouseY;
                            NewInput->MouseX = MouseX;
                        }
                    }
                    
                    for(int GamePadIndex = 0;
                        GamePadIndex < MAX_PLAYER_COUNT;
                        GamePadIndex++)
                    {
                        linux_gamepad *GamePadAt = &GamePads[GamePadIndex];
                        
                        if(GamePadAt->File != -1)
                        {
                            game_controller_input *OldController = GetController(OldInput, GamePadIndex + 1);
                            game_controller_input *NewController = GetController(NewInput, GamePadIndex + 1);
                            NewController->IsConnected = true;
                            
                            // TODO(luca): Cross frame values!!!
                            struct input_event InputEvents[64] = {};
                            psize BytesRead = 0;
                            
                            BytesRead = read(GamePadAt->File, InputEvents, sizeof(InputEvents));
                            if(BytesRead != -1)
                            {
                                for(u32 EventIndex = 0;
                                    EventIndex < ArrayCount(InputEvents);
                                    EventIndex++)
                                {
                                    struct input_event EventAt = InputEvents[EventIndex];
                                    
                                    switch(EventAt.type)
                                    {
                                        case EV_KEY:
                                        {
                                            b32 IsDown = EventAt.value;
                                            if(0) {}
                                            else if(EventAt.code == BTN_A)
                                            {
                                                NewController->ActionDown.EndedDown = IsDown;
                                            }
                                            else if(EventAt.code == BTN_B)
                                            {
                                                NewController->ActionRight.EndedDown = IsDown;
                                            }
                                            else if(EventAt.code == BTN_X)
                                            {
                                                NewController->ActionLeft.EndedDown = IsDown;
                                            }
                                            else if(EventAt.code == BTN_Y)
                                            {
                                                NewController->ActionUp.EndedDown = IsDown;
                                            }
                                            else if(EventAt.code == BTN_START)
                                            {
                                                NewController->Start.EndedDown = IsDown;
                                            }
                                            else if(EventAt.code == BTN_BACK)
                                            {
                                                NewController->Back.EndedDown = IsDown;
                                            }
                                        } break;
                                        
                                        case EV_ABS:
                                        {
                                            if(0) {}
                                            else if(EventAt.code == ABS_X)
                                            {
                                                NewController->IsAnalog = true;
                                                
                                                NewController->StickAverageX = LinuxNormalizeAxisValue(EventAt.value, GamePadAt->Axes[LSTICKX]);
                                            }
                                            else if(EventAt.code == ABS_Y)
                                            {
                                                NewController->StickAverageY = -1.0f * LinuxNormalizeAxisValue(EventAt.value, GamePadAt->Axes[LSTICKY]);
                                            }
                                            else if(EventAt.code == ABS_HAT0X)
                                            {
                                                NewController->StickAverageX = (r32)EventAt.value;
                                                NewController->IsAnalog = false;
                                            }
                                            else if(EventAt.code == ABS_HAT0Y)
                                            {
                                                NewController->StickAverageY = -(r32)EventAt.value;
                                                NewController->IsAnalog = false;
                                            }
                                        } break;
#if 0
                                        if(EventAt.type) printf("%d %d %d\n", EventAt.type, EventAt.code, EventAt.value);
#endif
                                    }
                                }
                            }
                        }
                    }
                    PROFILE_END;
                    
                    if(!GlobalPaused)
                    {
                        PROFILE_START_LABEL("Recording");
                        if(LinuxState.InputRecordingIndex)
                        {
                            LinuxRecordInput(&LinuxState, NewInput);
                        }
                        if(LinuxState.InputPlayingIndex)
                        {
                            LinuxPlayBackInput(&LinuxState, NewInput);
                        }
                        PROFILE_END;
                        // NOTE(luca): Clear buffer
                        
                        PROFILE_START_LABEL("Clear buffer");
                        MemorySet(WindowBuffer, 0, WindowBufferSize);
                        PROFILE_END;
                        
                        if(Game.UpdateAndRender)
                        {
                            PROFILE_START_LABEL("Update and Render");
                            Game.UpdateAndRender(&ThreadContext, &GameMemory, NewInput, &OffscreenBuffer);
                            PROFILE_END;
                        }
                        
                        
                        /* NOTE(luca): How sound works

Check the delay
Check the available frames in buffer

1. Too few audio frames in buffer for current frame.
-> Add more

2. Too many frames available
-> Add less / drain?

3. Fill first time?
-> Check delay
-> Maybe we should do this every frame?
-> Output needed frames to not have lag, this means to output maybe two frames?

TODO
- check if delay never changes
- check if buffersize never changes
-> cache these values
*/
                        
                        PROFILE_START_LABEL("Sound");
                        
                        r32 SamplesToWrite = 0;
                        local_persist b32 AudioFillFirstTime = true;
                        
                        r32 SingleFrameOfAudioFrames = TargetSecondsPerFrame*(r32)SampleRate;
                        
                        if(AudioFillFirstTime)
                        {
                            struct timespec WorkCounter = LinuxGetWallClock();
                            r32 WorkSecondsElapsed = LinuxGetSecondsElapsed(LastCounter, WorkCounter);
                            r32 SecondsLeftUntilFlip = TargetSecondsPerFrame - WorkSecondsElapsed;
                            
                            if(SecondsLeftUntilFlip > 0)
                            {
                                SamplesToWrite = (r32)SampleRate*(TargetSecondsPerFrame + SecondsLeftUntilFlip);
                            }
                            else
                            {
                                SamplesToWrite = SingleFrameOfAudioFrames;
                            }
                            
                            AudioFillFirstTime = false;
                        }
                        else
                        {
                            SamplesToWrite = SingleFrameOfAudioFrames;
                        }
                        
#if 0
                        snd_pcm_sframes_t AvailableFrames = 0;
                        snd_pcm_sframes_t DelayFrames;
                        
                        //snd_pcm_avail_update(PCMHandle);
                        AvailableFrames = snd_pcm_avail(PCMHandle);
                        snd_pcm_delay(PCMHandle, &DelayFrames);
                        
                        //printf("PeriodSize: %lu, PeriodTime: %d, BufferSize: %lu\n", PeriodSize, PeriodTime, PCMBufferSize);
                        printf("BeingWritten: %lu, Avail: %ld, Delay: %ld, ToWrite: %d\n", PCMBufferSize - AvailableFrames, AvailableFrames, DelayFrames, (s32)SamplesToWrite);
#endif
                        
                        game_sound_output_buffer SoundBuffer = {};
                        SoundBuffer.SamplesPerSecond = SampleRate;
                        SoundBuffer.SampleCount = (s32)SamplesToWrite;
                        // TODO(luca): This overflows according to the sanitizer.
                        SoundBuffer.Samples = (s16 *)AudioSamples;
                        
#if 0                        
                        if(Game.GetSoundSamples)
                        {
                            Game.GetSoundSamples(&ThreadContext, &GameMemory, &SoundBuffer);
                        }
                        LastFramesWritten = snd_pcm_writei(PCMHandle, SoundBuffer.Samples, SoundBuffer.SampleCount);
#endif
                        
                        if(LastFramesWritten < 0)
                        {
                            // TODO(luca): Logging
                            // NOTE(luca): We might want to silence in case of overruns ahead of time.  We also probably want to handle latency differently here. 
                            snd_pcm_recover(PCMHandle, LastFramesWritten, ALSA_RECOVER_SILENT);
                            
                            // underrun
                            if(LastFramesWritten == -EPIPE)
                            {
                                AudioFillFirstTime = true;
                            }
                        }
                        PROFILE_END;
                    }
                    
                    PROFILE_START_LABEL("Sleep");
#if 0                    
                    printf("Expected: %d, Delay: %4d, Being written: %5lu, Written: %d, Ptr: %lu\n", 
                           (int)SamplesToWrite, DelayFrames, PCMBufferSize - AvailableFrames, LastFramesWritten, PCMSoundStatus->hw_ptr);
#endif
                    
                    
                    struct timespec WorkCounter = LinuxGetWallClock();
                    
#if 1                    
                    r32 WorkMSPerFrame = (r32)((r32)LinuxGetNSecondsElapsed(LastCounter, WorkCounter)/1000000.f);
                    printf("%.2fms/f\n", WorkMSPerFrame);
#endif
                    
                    r32 SecondsElapsedForFrame = LinuxGetSecondsElapsed(LastCounter, WorkCounter);
                    if(SecondsElapsedForFrame < TargetSecondsPerFrame)
                    {
                        r32 SleepUS = ((TargetSecondsPerFrame - 0.001f - SecondsElapsedForFrame)*1000000.0f);
                        if(SleepUS > 0)
                        {
                            // TODO(luca): Intrinsic
                            usleep((u32)SleepUS);
                        }
                        else
                        {
                            // TODO(luca): Logging
                        }
                        
                        r32 TestSecondsElapsedForFrame = (r32)(LinuxGetSecondsElapsed(LastCounter, LinuxGetWallClock()));
                        if(TestSecondsElapsedForFrame < TargetSecondsPerFrame)
                        {
                            // TODO(luca): Log missed sleep
                        }
                        
                        // NOTE(luca): This is to help against sleep granularity.
                        while(SecondsElapsedForFrame < TargetSecondsPerFrame)
                        {
                            SecondsElapsedForFrame = LinuxGetSecondsElapsed(LastCounter, LinuxGetWallClock());
                        }
                    }
                    else
                    {
                        // TODO(luca): Log missed frame rate!
                    }
                    
                    struct timespec EndCounter = LinuxGetWallClock();
                    r32 MSPerFrame = (r32)((r32)LinuxGetNSecondsElapsed(LastCounter, EndCounter)/1000000.f);
                    LastCounter = EndCounter;
                    PROFILE_END;
                    
                    PROFILE_START_LABEL("Image");
                    
                    XPutImage(DisplayHandle, WindowHandle, DefaultGC, WindowImage, 0, 0, 0, 0, 
                              Width, Height);
                    
                    PROFILE_END;
                    FlipWallClock = LinuxGetWallClock();
                    
                    game_input *TempInput = NewInput;
                    NewInput = OldInput;
                    TempInput = NewInput;
                    
#if 0            
                    u64 EndCycleCount = __rdtsc();
                    u64 CyclesElapsed = EndCycleCount - LastCycleCount;
                    LastCycleCount = EndCycleCount;
                    
                    r64 FPS = 0;
                    r64 MCPF = (r64)(CyclesElapsed/(1000.0f*1000.0f));
                    
                    printf("%.2fms/f %.2ff/s %.2fmc/f\n", MSPerFrame, FPS, MCPF);
#endif
                }
                
#if HANDMADE_PROFILING
                spall_buffer_quit(&spall_ctx, &spall_buffer);
                spall_quit(&spall_ctx);
#endif
                
            }
            else
            {
                // TODO: Log this bad WindowHandle
            }
        }
        else
        {
            // TODO: Log this could not match visual info
        }
    }
    else
    {
        // TODO: Log could not get x connection 
    }
    
    return 0;
}
