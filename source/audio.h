#pragma once
#include <3ds.h>
#include <stdbool.h>

#define AUDIO_CHANNEL    0
#define SAMPLE_RATE      44100
#define BUFFER_SIZE      (SAMPLE_RATE * 2)   // 2 seconds of stereo s16
#define PITCH_MIN       -12.0f               // semitones
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

    float        pitch;        // semitones, -12 to +12
    float        speed;        // multiplier, 0.25 to 4.0
    float        volume;       // 0.0 to 1.0

    // Timing
    double       duration;     // seconds
    double       position;     // seconds

    // Internal decoder handle (cast to format-specific struct)
    bool         ndsp_available;
    void*        decoder;

    // NDSP wave buffers (double-buffered)
    ndspWaveBuf  wave_buf[2];
    s16*         pcm_buf[2];
    int          active_buf;

    // Pitch/speed processing scratch buffer
    s16*         process_buf;
    int          process_buf_size;
} AudioState;

void  audio_init(AudioState* a);
void  audio_shutdown(AudioState* a);
void  audio_play(AudioState* a, const char* path);
void  audio_stop(AudioState* a);
void  audio_toggle_pause(AudioState* a);
void  audio_update(AudioState* a);          // call every frame
void  audio_adjust_pitch(AudioState* a, float semitones);
void  audio_adjust_speed(AudioState* a, float delta);
void  audio_reset_fx(AudioState* a);
void  audio_set_volume(AudioState* a, float vol);

// Returns 0.0-1.0 progress through track
float audio_progress(const AudioState* a);

// Fills buf[0..n-1] with normalized waveform samples for visualizer
void  audio_get_waveform(const AudioState* a, float* buf, int n);
