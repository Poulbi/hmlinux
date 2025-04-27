#include <stdio.h>
#include <time.h>
#include <x86intrin.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <linux/limits.h>
#include <linux/input.h>
#include <alsa/asoundlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysymdef.h>
#include <X11/extensions/Xrandr.h>

#include "handmade.h"
#include "linux_handmade.h"

#define true 1
#define false 0

#define MAX_PLAYER_COUNT 4

// NOTE(luca): Bits are layed out over multiple bytes.  This macro checks which byte the bit will be set in.
#define IsEvdevBitSet(Bit, Array) (Array[(Bit) / 8] & (1 << ((Bit) % 8)))
#define BytesNeededForBits(Bits) ((Bits + 7) / 8)

global_variable b32 GlobalRunning;
global_variable b32 GlobalPaused;

void Memcpy(char *Dest, char *Source, size_t Count)
{
    while(Count--) *Dest++ = *Source++;
}

int StrLen(char *String)
{
    size_t Result = 0;
    
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

internal void LinuxGetAxisInfo(linux_gamepad *GamePad, linux_gamepad_axes_enum Axis, int AbsAxis)
{
    input_absinfo AxesInfo = {};
    if(ioctl(GamePad->FileFD, EVIOCGABS(AbsAxis), &AxesInfo) != -1)
    {
        GamePad->Axes[Axis].Minimum = AxesInfo.minimum;
        GamePad->Axes[Axis].Maximum = AxesInfo.maximum;
        GamePad->Axes[Axis].Fuzz = AxesInfo.fuzz;
        GamePad->Axes[Axis].Flat = AxesInfo.flat;
    }
}

internal void LinuxOpenGamePad(char *FilePath, linux_gamepad *GamePad)
{
    GamePad->FileFD = open(FilePath, O_RDWR|O_NONBLOCK);
    
    if(GamePad->FileFD != -1)
    {
        int Version = 0;
        int IsCompatible = true;
        
        // TODO(luca): Check versions
        ioctl(GamePad->FileFD, EVIOCGVERSION, &Version);
        ioctl(GamePad->FileFD, EVIOCGNAME(sizeof(GamePad->Name)), GamePad->Name);
        
        char SupportedEventBits[BytesNeededForBits(EV_MAX)] = {};
        if(ioctl(GamePad->FileFD, EVIOCGBIT(0, sizeof(SupportedEventBits)), SupportedEventBits) != -1)
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
        if(ioctl(GamePad->FileFD, EVIOCGBIT(EV_KEY , sizeof(SupportedKeyBits)), SupportedKeyBits) != -1)
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
            
            Memcpy(GamePad->FilePath, FilePath, StrLen(FilePath));
        }
        else
        {
            close(GamePad->FileFD);
            *GamePad = {};
            GamePad->FileFD = -1;
        }
    }
}

// TODO(luca): Make this work in the case of multiple displays.
internal r32 LinuxGetMonitorRefreshRate(Display *DisplayHandle, Window RootWindow)
{
    r32 Result = 0;
    
    void *LibraryHandle = dlopen("libXrandr.so", RTLD_NOW);
    
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

internal void LinuxGetLibraryPath(char *Dest, char *ExecutablePath, char *LibraryFileName)
{
    size_t LastSlash = 0;
    for(char *Scan = ExecutablePath;
        *Scan;
        Scan++)
    {
        if(*Scan == '/')
        {
            LastSlash = Scan - ExecutablePath;
        }
    }
    
    for(size_t Index = 0;
        Index < LastSlash + 1;
        Index++)
    {
        *Dest++ = *ExecutablePath++;
    }
    
    while(*LibraryFileName) *Dest++ = *LibraryFileName++;
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

internal void LinuxProcessPendingMessages(Display *DisplayHandle, Window WindowHandle,
                                          Atom WM_DELETE_WINDOW, game_controller_input *KeyboardController)
{
    XEvent WindowEvent = {};
    while(XPending(DisplayHandle) > 0)
    {
        XNextEvent(DisplayHandle, &WindowEvent);
        switch (WindowEvent.type)
        {
            case KeyPress:
            case KeyRelease:
            {
                KeySym keysym = XLookupKeysym(&WindowEvent.xkey, 0);
                
                b32 IsDown = (WindowEvent.type == KeyPress);
                
                if(0) {}
                else if(keysym == XK_w)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveUp, IsDown);
                }
                else if(keysym == XK_a)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveLeft, IsDown);
                }
                else if(keysym == XK_r)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveDown, IsDown);
                }
                else if(keysym == XK_s)
                {
                    LinuxProcessKeyPress(&KeyboardController->MoveRight, IsDown);
                }
                else if(keysym == XK_Up)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionUp, IsDown);
                }
                else if(keysym == XK_Left)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionLeft, IsDown);
                }
                else if(keysym == XK_Down)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionDown, IsDown);
                }
                else if(keysym == XK_Right)
                {
                    LinuxProcessKeyPress(&KeyboardController->ActionRight, IsDown);
                }
                else if(keysym == XK_p)
                {
                    if(IsDown)
                    {
                        GlobalPaused = !GlobalPaused;
                    }
                }
                else if(keysym == XK_Escape ||
                        keysym == XK_q)
                {
                    GlobalRunning = false;
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
        }
    }
    
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

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile)
{
    debug_read_file_result Result = {};
    
    int FD = open(FileName, O_RDONLY);
    if(FD != -1)
    {
        struct stat FileStats = {};
        fstat(FD, &FileStats);
        Result.ContentsSize = FileStats.st_size;
        Result.Contents = mmap(0, FileStats.st_size, PROT_READ, MAP_PRIVATE, FD, 0);
        
        close(FD);
    }
    
    return Result;
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

internal s64 LinuxGetSecondsElapsed(struct timespec Start, struct timespec End)
{
    s64 Result = 0;
    Result = ((s64)End.tv_sec*1000000000 + (s64)End.tv_nsec) - ((s64)Start.tv_sec*1000000000 + (s64)Start.tv_nsec);
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

int main(int ArgC, char *Args[])
{
    Display *DisplayHandle = XOpenDisplay(0);
    
    if(DisplayHandle)
    {
        Window RootWindow = XDefaultRootWindow(DisplayHandle);
        int Screen = XDefaultScreen(DisplayHandle);
        int Width = 800;
        int Height = 600;
        int ScreenBitDepth = 24;
        XVisualInfo WindowVisualInfo = {};
        if(XMatchVisualInfo(DisplayHandle, Screen, ScreenBitDepth, TrueColor, &WindowVisualInfo))
        {
            XSetWindowAttributes WindowAttributes = {};
            WindowAttributes.bit_gravity = StaticGravity;
            WindowAttributes.background_pixel = 0;
            WindowAttributes.colormap = XCreateColormap(DisplayHandle, RootWindow, WindowVisualInfo.visual, AllocNone);
            WindowAttributes.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask;
            u64 WindowAttributeMask = CWBitGravity | CWBackPixel | CWColormap | CWEventMask;
            
            Window WindowHandle = XCreateWindow(DisplayHandle, RootWindow,
                                                1920 - Width - 2, 1080 - Height - 2,
                                                Width, Height,
                                                0,
                                                WindowVisualInfo.depth, InputOutput,
                                                WindowVisualInfo.visual, WindowAttributeMask, &WindowAttributes);
            if(WindowHandle)
            {
                XStoreName(DisplayHandle, WindowHandle, "Handmade Window");
                LinuxSetSizeHint(DisplayHandle, WindowHandle, Width, Height, Width, Height);
                
                Atom WM_DELETE_WINDOW = XInternAtom(DisplayHandle, "WM_DELETE_WINDOW", False);
                if(!XSetWMProtocols(DisplayHandle, WindowHandle, &WM_DELETE_WINDOW, 1))
                {
                    printf("Couldn't register WM_DELETE_WINDOW property\n");
                }
                
                XClassHint ClassHint = {};
                ClassHint.res_name = "Handmade Window";
                ClassHint.res_class = "Handmade Window";
                XSetClassHint(DisplayHandle, WindowHandle, &ClassHint);
                
                int BitsPerPixel = 32;
                int BytesPerPixel = BitsPerPixel/8;
                int WindowBufferSize = Width*Height*BytesPerPixel;
                char *WindowBufferMemory = (char *)malloc(WindowBufferSize);
                
                XImage *WindowBuffer = XCreateImage(DisplayHandle, WindowVisualInfo.visual, WindowVisualInfo.depth, ZPixmap, 0, WindowBufferMemory, Width, Height, BitsPerPixel, 0);
                GC DefaultGC = DefaultGC(DisplayHandle, Screen);
                
                char LibraryFullPath[PATH_MAX] = {};
                char LibraryName[] = "handmade.so";
                LinuxGetLibraryPath(LibraryFullPath, Args[0], LibraryName);
                linux_game_code Game = LinuxLoadGameCode(LibraryFullPath);
                Game.LibraryLastWriteTime = LinuxGetLastWriteTime(LibraryFullPath);
                
                game_memory GameMemory = {};
                GameMemory.PermanentStorageSize = Megabytes(64);
                GameMemory.TransientStorageSize = Gigabytes(1);
                GameMemory.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
                GameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;
                GameMemory.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
                
#if HANDMADE_INTERNAL
                void *BaseAddress = (void *)Terabytes(2);
#else
                void *BaseAddress = 0;
#endif
                
                u64 TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;
                GameMemory.PermanentStorage = (char *)mmap(BaseAddress, TotalSize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
                GameMemory.TransientStorage = (u8 *)GameMemory.PermanentStorage + GameMemory.PermanentStorageSize;
                
                game_input Input[2] = {};
                game_input *NewInput = &Input[0];
                game_input *OldInput = &Input[1];
                
                linux_gamepad GamePads[MAX_PLAYER_COUNT] = {};
                for(int GamePadIndex = 0;
                    GamePadIndex < MAX_PLAYER_COUNT;
                    GamePadIndex++)
                {
                    GamePads[GamePadIndex].FileFD = -1;
                }
                
                // TODO(luca): Run this once on startup to detect connected controllers and later with inotify when controllers are added.
                char EventDirectoryName[] = "/dev/input/";
                DIR *EventDirectory = opendir(EventDirectoryName);
                struct dirent *Entry = 0;
                int GamePadIndex = 0;
                while((Entry = readdir(EventDirectory)))
                {
                    if(!strncmp(Entry->d_name, "event", sizeof("event") - 1))
                    {
                        char FilePath[PATH_MAX] = {};
                        CatStrings(sizeof(EventDirectoryName) - 1, EventDirectoryName,
                                   StrLen(Entry->d_name), Entry->d_name,
                                   sizeof(FilePath) - 1, FilePath);
                        if(GamePadIndex < MAX_PLAYER_COUNT)
                        {
                            linux_gamepad *GamePadAt = &GamePads[GamePadIndex];
                            LinuxOpenGamePad(FilePath, GamePadAt);
                            if(GamePadAt->FileFD != -1)
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
                
                game_offscreen_buffer OffscreenBuffer = {};
                OffscreenBuffer.Memory = WindowBufferMemory;
                OffscreenBuffer.Width = Width;
                OffscreenBuffer.Height = Height;
                OffscreenBuffer.BytesPerPixel = BytesPerPixel;
                OffscreenBuffer.Pitch = Width*BytesPerPixel;
                
                int LastFramesWritten = 0;
                unsigned int SampleRate, ChannelCount, TimePeriod, SampleCount;
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
                snd_pcm_hw_params_get_period_time(PCMParams, &TimePeriod, NULL);
                snd_pcm_hw_params_get_buffer_size(PCMParams, &PCMBufferSize);
                snd_pcm_status_malloc(&PCMStatus);
                
                char AudioSamples[PCMBufferSize];
                u64 Periods = 2;
                u32 BytesPerSample = (sizeof(s16)*ChannelCount);
                
#if 1              
                r32 GameUpdateHz = 30;
#else
                r32 GameUpdateHz = LinuxGetMonitorRefreshRate(DisplayHandle, RootWindow);
#endif
                
                thread_context ThreadContext = {};
                
                XMapWindow(DisplayHandle, WindowHandle);
                XFlush(DisplayHandle);
                
                struct timespec LastCounter = LinuxGetWallClock();
                struct timespec FlipWallClock = LinuxGetWallClock();
                r32 TargetSecondsPerFrame = 1.0f / GameUpdateHz; 
                
                GlobalRunning = true;
                
                u64 LastCycleCount = __rdtsc();
                while(GlobalRunning)
                {
                    
#if HANDMADE_INTERNAL
                    // NOTE(luca): Because gcc will first create an empty file and then write into it we skip trying to reload when the file is empty.
                    struct stat FileStats = {};
                    stat(LibraryFullPath, &FileStats);
                    if(FileStats.st_size)
                    {
                        s64 SecondsElapsed = LinuxGetSecondsElapsed(Game.LibraryLastWriteTime, FileStats.st_mtim) / 1000/1000;
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
                    
                    LinuxProcessPendingMessages(DisplayHandle, WindowHandle, WM_DELETE_WINDOW, NewKeyboardController);
                    
                    // TODO(luca): Use buttonpress/release events instead so we query this less frequently.
                    s32 MouseX = 0, MouseY = 0, MouseZ = 0; // TODO(luca): Support mousewheel?
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
                            NewInput->MouseButtons[0].EndedDown = ((MouseMask & Button1Mask) > 0); 
                            NewInput->MouseButtons[1].EndedDown = ((MouseMask & Button2Mask) > 0); 
                            NewInput->MouseButtons[2].EndedDown = ((MouseMask & Button3Mask) > 0); 
                        }
                    }
                    
                    for(int GamePadIndex = 0;
                        GamePadIndex < MAX_PLAYER_COUNT;
                        GamePadIndex++)
                    {
                        linux_gamepad *GamePadAt = &GamePads[GamePadIndex];
                        
                        if(GamePadAt->FileFD != -1)
                        {
                            game_controller_input *OldController = GetController(OldInput, 0);
                            game_controller_input *NewController = GetController(NewInput, 0);
                            
                            // TODO(luca): What we really want is the last event. (or haltransitions)
                            struct input_event InputEvents[64] = {};
                            int BytesRead = 0;
                            
                            BytesRead = read(GamePadAt->FileFD, InputEvents, sizeof(InputEvents));
                            if(BytesRead != -1)
                            {
                                for(int EventIndex = 0;
                                    EventIndex < (int)ArrayCount(InputEvents);
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
                                                NewController->StickAverageX = EventAt.value;
                                                NewController->IsAnalog = false;
                                            }
                                            else if(EventAt.code == ABS_HAT0Y)
                                            {
                                                NewController->StickAverageY = -EventAt.value;
                                                NewController->IsAnalog = false;
                                            }
                                        } break;
                                    }
#if 0
                                    if(EventAt.type) printf("%d %d %d\n", EventAt.type, EventAt.code, EventAt.value);
#endif
                                }
                                
                            }
                        }
                    }
                    
                    
                    // NOTE(luca): Buffer is cleared just in case.
#if 1
                    for(int X = 0; X < OffscreenBuffer.Width; X++)
                    {
                        for(int Y = 0; Y < OffscreenBuffer.Height; Y++)
                        {
                            ((u32 *)OffscreenBuffer.Memory)[X + Y*OffscreenBuffer.Width] = 0;
                        }
                    }
#endif
                    
                    if(!GlobalPaused)
                    {
                        if(Game.UpdateAndRender)
                        {
                            Game.UpdateAndRender(&ThreadContext, &GameMemory, NewInput, &OffscreenBuffer);
                        }
                    }
                    
                    r32 SamplesToWrite = TargetSecondsPerFrame*SampleRate;
                    local_persist b32 AudioFillFirstTime = true;
                    r32 SecondsSinceFlip = (LinuxGetSecondsElapsed(FlipWallClock, LinuxGetWallClock()))/1000000000.0f;
                    r32 SecondsLeftUntilFlip = TargetSecondsPerFrame - SecondsSinceFlip;
                    if(AudioFillFirstTime)
                    {
                        SamplesToWrite += SecondsLeftUntilFlip*SampleRate;
                        AudioFillFirstTime = false;
                    }
                    
#if HANDMADE_INTERNAL                    
                    snd_htimestamp_t AudioTimeStamp = {};
                    snd_pcm_status(PCMHandle, PCMStatus);
                    sound_status *PCMSoundStatus = (sound_status *)PCMStatus;
                    u32 AvailableFrames = snd_pcm_status_get_avail(PCMStatus);
                    s32 DelayFrames = snd_pcm_status_get_delay(PCMStatus);
                    snd_pcm_status_get_htstamp(PCMStatus, &AudioTimeStamp);
                    struct timespec AudioWallClock = LinuxGetWallClock();
                    
#if 0                    
                    // TODO(luca): Why does this work?
                    if(DelayFrames > SamplesToWrite && DelayFrames > 0)
                    {
                        SamplesToWrite -= (DelayFrames - SamplesToWrite);
                        if(SamplesToWrite < 0)
                        {
                            SamplesToWrite = 0;
                        }
                    }
#endif
                    
                    u32 PadY = 16;
                    u32 LineHeight = 38;
                    u32 X = 0; 
                    u32 Y = 0;
                    
                    u32 MapIntoWidth = (OffscreenBuffer.Width);
                    
                    X = MapIntoWidth*(r32)((r32)SamplesToWrite/(r32)SampleRate);
                    LinuxDebugVerticalLine(&OffscreenBuffer, X, Y*LineHeight + PadY, 0x0030FF00);
                    
                    X = MapIntoWidth*(r32)((r32)AvailableFrames/(r32)PCMBufferSize);
                    LinuxDebugVerticalLine(&OffscreenBuffer, X, Y*LineHeight + PadY, 0x0030FF00);
                    
                    X = MapIntoWidth*(r32)((r32)DelayFrames/(r32)PCMBufferSize);
                    LinuxDebugVerticalLine(&OffscreenBuffer, X, Y*LineHeight + PadY, 0x00FF00FF);
                    
                    Y++;
                    
                    X = MapIntoWidth*(r32)((r32)SecondsSinceFlip/TargetSecondsPerFrame);
                    LinuxDebugVerticalLine(&OffscreenBuffer, X, Y*LineHeight + PadY, 0x0000FF00);
                    
#endif
                    
                    game_sound_output_buffer SoundBuffer = {};
                    SoundBuffer.SamplesPerSecond = SampleRate;
                    SoundBuffer.SampleCount = SamplesToWrite;
                    SoundBuffer.Samples = (s16 *)AudioSamples;
                    
                    if(Game.GetSoundSamples)
                    {
                        Game.GetSoundSamples(&ThreadContext, &GameMemory, &SoundBuffer);
                    }
                    
                    LastFramesWritten = snd_pcm_writei(PCMHandle, SoundBuffer.Samples, SoundBuffer.SampleCount);
                    
                    if(LastFramesWritten < 0)
                    {
                        // TODO(luca): Logging
                        // NOTE(luca): We might want to silence in case of overruns ahead of time.  We also probably want to handle latency differently here. 
                        snd_pcm_recover(PCMHandle, LastFramesWritten, 0);
                    }
                    
                    printf("Expected: %d, Delay: %4d, Being written: %5lu, Written: %d, Ptr: %lu\n", 
                           (int)SamplesToWrite, DelayFrames, PCMBufferSize - AvailableFrames, LastFramesWritten, PCMSoundStatus->hw_ptr);
                    
                    struct timespec WorkCounter = LinuxGetWallClock();
                    r32 WorkSecondsElapsed = LinuxGetSecondsElapsed(LastCounter, WorkCounter);
                    r32 SecondsElapsedForFrame = (WorkSecondsElapsed/1000000000.0f);
                    if(SecondsElapsedForFrame < TargetSecondsPerFrame)
                    {
                        
                        s64 SleepUS = (s64)((TargetSecondsPerFrame - 0.001 - SecondsElapsedForFrame)*1000000.0f);
                        if(SleepUS > 0)
                        {
                            usleep(SleepUS);
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
                        
                        while(SecondsElapsedForFrame < TargetSecondsPerFrame)
                        {
                            SecondsElapsedForFrame = LinuxGetSecondsElapsed(LastCounter, LinuxGetWallClock())/1000000000.0f;
                        }
                    }
                    else
                    {
                        // TODO(luca): Log missed frame rate!
                    }
                    
                    struct timespec EndCounter = LinuxGetWallClock();
                    r32 MSPerFrame = (r32)(LinuxGetSecondsElapsed(LastCounter, EndCounter)/1000000.f);
                    LastCounter = EndCounter;
                    
                    XPutImage(DisplayHandle, WindowHandle, DefaultGC, WindowBuffer, 0, 0, 0, 0, Width, Height);
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
