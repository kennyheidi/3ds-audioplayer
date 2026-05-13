#pragma once
#include <3ds.h>
#include <stdbool.h>

#define AUDIO_CHANNEL    0
#define SAMPLE_RATE      44100

// OLD 3DS BUFFER TUNING:
// Each wave buffer holds this many stereo s16 frames.
// Bigger = more decode time before a gap, but more latency.
// 4096 frames = ~93ms at 44100Hz — enough headroom for O3DS MP3 decode.
// We keep 4 buffers (was 2) so the DSP always has 2 queued ahead.
#define FRAMES_PER_BUF   8192
#define BUFFER_SIZE      (FRAMES_PER_BUF * 2)  // *2 for stereo s16 samples
#define NUM_BUFS         4                       // was 2 — extra cushion for O3DS

#define PITCH_MIN       -12.0f
#define PITCH_MAX        12.0f
#define SPEED_MIN        0.25f
#define SPEED_MAX        4.0f

typedef enum {
    AUDIO_STOPPED,
    AUDIO_PLAYING,
    AUDIO_PAUSED
} AudioStatus;

typedef enum {
    FMT_UNKNOWN,
    FMT_WAV,
    FMT_MP3,
    FMT_OGG,
    FMT_FLAC
} AudioFormat;

typedef struct {
    AudioStatus  status;
    AudioFormat  format;
    char         current_file[512];
    char         current_title[256];

    float        pitch;
    float        speed;
    float        volume;

    double       duration;
    double       position;

    bool         ndsp_available;
    void*        decoder;

    // Expanded to NUM_BUFS buffers for Old 3DS headroom
    ndspWaveBuf  wave_buf[NUM_BUFS];
    s16*         pcm_buf[NUM_BUFS];
    int          active_buf;

    s16*         process_buf;
    int          process_buf_size;
} AudioState;

void  audio_init(AudioState* a);
void  audio_shutdown(AudioState* a);
void  audio_play(AudioState* a, const char* path);
void  audio_stop(AudioState* a);
void  audio_toggle_pause(AudioState* a);
void  audio_update(AudioState* a);
void  audio_adjust_pitch(AudioState* a, float semitones);
void  audio_adjust_speed(AudioState* a, float delta);
void  audio_reset_fx(AudioState* a);
void  audio_set_volume(AudioState* a, float vol);
float audio_progress(const AudioState* a);
void  audio_get_waveform(const AudioState* a, float* buf, int n);
