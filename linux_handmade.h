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
    int FileFD;
    char FilePath[PATH_MAX];
    
    char Name[256];
    int SupportsRumble;
    linux_gamepad_axis Axes[AXES_COUNT];
};



// IMPORTANT(luca): Copy Pasted from alsa-lib;  This is a hack to access the hw_ptr and to
// understand what alsa-lib functions are doing internally better through the debugger.
typedef struct { unsigned char pad[sizeof(time_t) - sizeof(int)]; } __time_pad;
typedef struct
{
	snd_pcm_state_t state;		     /* stream state */
	__time_pad pad1;		           /* align to timespec */
	struct timespec trigger_tstamp;	/* time when stream was started/stopped/paused */
	struct timespec tstamp;		    /* reference timestamp */
	snd_pcm_uframes_t appl_ptr;	    /* appl ptr */
	snd_pcm_uframes_t hw_ptr;	      /* hw ptr */
	snd_pcm_sframes_t delay;	       /* current delay in frames */
	snd_pcm_uframes_t avail;	       /* number of frames available */
	snd_pcm_uframes_t avail_max;	   /* max frames available on hw since last status */
	snd_pcm_uframes_t overrange;	   /* count of ADC (capture) overrange detections from last status */
	snd_pcm_state_t suspended_state;   /* suspended stream state */
	__u32 audio_tstamp_data;	       /* needed for 64-bit alignment, used for configs/report to/from userspace */
	struct timespec audio_tstamp;	  /* sample counter, wall clock, PHC or on-demand sync'ed */
	struct timespec driver_tstamp;     /* useful in case reference system tstamp is reported with delay */
	__u32 audio_tstamp_accuracy;	   /* in ns units, only valid if indicated in audio_tstamp_data */
	unsigned char reserved[52-2*sizeof(struct timespec)]; /* must be filled with zero */
} sound_status;

#endif //LINUX_HANDMADE_H
