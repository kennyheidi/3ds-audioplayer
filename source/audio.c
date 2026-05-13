// audio.c — drop‑in replacement tuned for Old 3DS XL
// Lag reduction: larger NDSP buffers, decode thread, PCM ring buffer, fixed‑point pitch/speed.

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "audio.h"
#include "filebrowser.h"

// ---- Config ----

#define AUDIO_CHANNEL        0
#define AUDIO_SAMPLE_RATE    44100
#define AUDIO_FORMAT         NDSP_FORMAT_STEREO_PCM16

// NDSP buffers (what the DSP pulls from)
#define NDSP_BUFFER_SAMPLES  4096
#define NDSP_NUM_BUFFERS     3

// Ring buffer (decoded PCM ahead of time)
#define PCM_RING_SAMPLES     (NDSP_BUFFER_SAMPLES * 8)

// Fixed‑point for pitch/speed (16.16)
#define FP_SHIFT             16
#define FP_ONE               (1 << FP_SHIFT)

// ---- Types ----

typedef enum {
    AUDIO_TYPE_NONE = 0,
    AUDIO_TYPE_MP3,
    AUDIO_TYPE_OGG,
    AUDIO_TYPE_FLAC,
    AUDIO_TYPE_WAV
} AudioType;

// Forward‑declared decoder state structs (defined in your existing decoder .c/.h)
typedef struct Mp3Decoder Mp3Decoder;
typedef struct OggDecoder OggDecoder;
typedef struct FlacDecoder FlacDecoder;
typedef struct WavDecoder WavDecoder;

// ---- External decoder API (must match your existing repo) ----
// You already have these in your project; keep signatures identical.

extern Mp3Decoder* mp3_open(const char* path, int* sampleRate, int* channels);
extern int         mp3_read(Mp3Decoder* dec, int16_t* out, int frames);
extern void        mp3_close(Mp3Decoder* dec);

extern OggDecoder* ogg_open(const char* path, int* sampleRate, int* channels);
extern int         ogg_read(OggDecoder* dec, int16_t* out, int frames);
extern void        ogg_close(OggDecoder* dec);

extern FlacDecoder* flac_open(const char* path, int* sampleRate, int* channels);
extern int          flac_read(FlacDecoder* dec, int16_t* out, int frames);
extern void         flac_close(FlacDecoder* dec);

extern WavDecoder* wav_open(const char* path, int* sampleRate, int* channels);
extern int         wav_read(WavDecoder* dec, int16_t* out, int frames);
extern void        wav_close(WavDecoder* dec);

// ---- Global playback state ----

static volatile bool g_audioInitialized = false;
static volatile bool g_playing          = false;
static volatile bool g_paused           = false;
static volatile bool g_stopRequested    = false;

// Pitch in semitones, speed as 16.16 fixed‑point multiplier
static volatile int  g_pitchSemitones   = 0;        // −12..+12
static volatile s32  g_speedFP          = FP_ONE;   // 0.25×..4.0×

static AudioType     g_audioType        = AUDIO_TYPE_NONE;
static Mp3Decoder*   g_mp3              = NULL;
static OggDecoder*   g_ogg              = NULL;
static FlacDecoder*  g_flac             = NULL;
static WavDecoder*   g_wav              = NULL;
static int           g_srcSampleRate    = AUDIO_SAMPLE_RATE;
static int           g_srcChannels      = 2;

// ---- NDSP buffers ----

static ndspWaveBuf   g_waveBuf[NDSP_NUM_BUFFERS];
static int16_t       g_ndspBuffers[NDSP_NUM_BUFFERS][NDSP_BUFFER_SAMPLES * 2]; // stereo

// ---- PCM ring buffer (decoded ahead) ----

static int16_t       g_pcmRing[PCM_RING_SAMPLES * 2]; // stereo
static volatile u32  g_pcmWritePos = 0;               // in frames
static volatile u32  g_pcmReadPos  = 0;               // in frames

// ---- Decode thread ----

static Thread        g_decodeThread   = 0;
static volatile bool g_decodeRunning  = false;

// ---- Utility ----

static inline u32 ring_capacity_frames(void) {
    return PCM_RING_SAMPLES;
}

static inline u32 ring_used_frames(void) {
    u32 w = g_pcmWritePos;
    u32 r = g_pcmReadPos;
    if (w >= r) return w - r;
    return ring_capacity_frames() - (r - w);
}

static inline u32 ring_free_frames(void) {
    return ring_capacity_frames() - ring_used_frames() - 1;
}

static void ring_write_frames(const int16_t* src, u32 frames) {
    u32 cap = ring_capacity_frames();
    for (u32 i = 0; i < frames; i++) {
        u32 idx = (g_pcmWritePos + i) % cap;
        g_pcmRing[idx * 2 + 0] = src[i * 2 + 0];
        g_pcmRing[idx * 2 + 1] = src[i * 2 + 1];
    }
    g_pcmWritePos = (g_pcmWritePos + frames) % cap;
}

static u32 ring_read_frames(int16_t* dst, u32 frames) {
    u32 available = ring_used_frames();
    if (frames > available) frames = available;
    u32 cap = ring_capacity_frames();
    for (u32 i = 0; i < frames; i++) {
        u32 idx = (g_pcmReadPos + i) % cap;
        dst[i * 2 + 0] = g_pcmRing[idx * 2 + 0];
        dst[i * 2 + 1] = g_pcmRing[idx * 2 + 1];
    }
    g_pcmReadPos = (g_pcmReadPos + frames) % cap;
    return frames;
}

// ---- Decoder dispatch ----

static int decoder_read_frames(int16_t* out, int frames) {
    if (!g_playing || g_stopRequested) return 0;

    switch (g_audioType) {
        case AUDIO_TYPE_MP3:
            return mp3_read(g_mp3, out, frames);
        case AUDIO_TYPE_OGG:
            return ogg_read(g_ogg, out, frames);
        case AUDIO_TYPE_FLAC:
            return flac_read(g_flac, out, frames);
        case AUDIO_TYPE_WAV:
            return wav_read(g_wav, out, frames);
        default:
            return 0;
    }
}

static void decoder_close(void) {
    switch (g_audioType) {
        case AUDIO_TYPE_MP3:
            if (g_mp3) mp3_close(g_mp3);
            g_mp3 = NULL;
            break;
        case AUDIO_TYPE_OGG:
            if (g_ogg) ogg_close(g_ogg);
            g_ogg = NULL;
            break;
        case AUDIO_TYPE_FLAC:
            if (g_flac) flac_close(g_flac);
            g_flac = NULL;
            break;
        case AUDIO_TYPE_WAV:
            if (g_wav) wav_close(g_wav);
            g_wav = NULL;
            break;
        default:
            break;
    }
    g_audioType = AUDIO_TYPE_NONE;
}

// ---- Pitch / speed ----

static void update_ndsp_pitch_speed(void) {
    // Pitch in semitones → frequency ratio
    float pitchRatio = powf(2.0f, (float)g_pitchSemitones / 12.0f);

    // Speed is fixed‑point multiplier
    float speedRatio = (float)g_speedFP / (float)FP_ONE;

    float totalRatio = pitchRatio * speedRatio;
    float newRate    = (float)AUDIO_SAMPLE_RATE * totalRatio;

    ndspChnSetRate(AUDIO_CHANNEL, newRate);
}

// ---- Decode thread func ----

static void decode_thread_func(void* arg) {
    (void)arg;

    const int tempFrames = 1024;
    static int16_t tempBuf[1024 * 2];

    while (g_decodeRunning) {
        if (!g_playing || g_paused || g_stopRequested) {
            svcSleepThread(5 * 1000 * 1000); // 5 ms
            continue;
        }

        // Fill ring buffer while there's space
        u32 freeFrames = ring_free_frames();
        if (freeFrames < (u32)tempFrames) {
            // Not enough space, chill a bit
            svcSleepThread(2 * 1000 * 1000); // 2 ms
            continue;
        }

        int got = decoder_read_frames(tempBuf, tempFrames);
        if (got <= 0) {
            // End of file or error
            g_stopRequested = true;
            g_playing       = false;
            continue;
        }

        ring_write_frames(tempBuf, (u32)got);
    }

    threadExit(0);
}

// ---- NDSP callback / refill ----

static void refill_wavebuf(ndspWaveBuf* buf) {
    if (!g_playing || g_stopRequested) {
        memset(buf->data_vaddr, 0, NDSP_BUFFER_SAMPLES * 2 * sizeof(int16_t));
        buf->nsamples = NDSP_BUFFER_SAMPLES;
        return;
    }

    int16_t* dst = (int16_t*)buf->data_vaddr;
    u32 got = ring_read_frames(dst, NDSP_BUFFER_SAMPLES);

    if (got < NDSP_BUFFER_SAMPLES) {
        // Underrun: pad with zeros
        memset(dst + got * 2, 0, (NDSP_BUFFER_SAMPLES - got) * 2 * sizeof(int16_t));
    }

    buf->nsamples = NDSP_BUFFER_SAMPLES;
}

static void audio_frame_callback(void* arg) {
    (void)arg;

    for (int i = 0; i < NDSP_NUM_BUFFERS; i++) {
        ndspWaveBuf* wb = &g_waveBuf[i];
        if (wb->status == NDSP_WBUF_DONE) {
            refill_wavebuf(wb);
            ndspChnWaveBufAdd(AUDIO_CHANNEL, wb);
        }
    }
}

// ---- Public API ----

bool audio_init(void) {
    if (g_audioInitialized) return true;

    if (ndspInit() != 0) {
        return false;
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(AUDIO_CHANNEL, AUDIO_SAMPLE_RATE);
    ndspChnSetFormat(AUDIO_CHANNEL, AUDIO_FORMAT);

    // Identity mix (L→L, R→R)
    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f; // L
    mix[1] = 1.0f; // R
    ndspChnSetMix(AUDIO_CHANNEL, mix);

    // Setup wave buffers
    memset(g_waveBuf, 0, sizeof(g_waveBuf));
    for (int i = 0; i < NDSP_NUM_BUFFERS; i++) {
        g_waveBuf[i].data_vaddr = g_ndspBuffers[i];
        g_waveBuf[i].nsamples   = NDSP_BUFFER_SAMPLES;
        g_waveBuf[i].looping    = false;
        DSP_FlushDataCache(g_ndspBuffers[i], NDSP_BUFFER_SAMPLES * 2 * sizeof(int16_t));
        ndspChnWaveBufAdd(AUDIO_CHANNEL, &g_waveBuf[i]);
    }

    // Set callback
    ndspSetCallback(audio_frame_callback, NULL);

    // Start decode thread
    g_decodeRunning = true;
    g_decodeThread = threadCreate(
        decode_thread_func,
        NULL,
        32 * 1024,
        0x18,
        -2,
        false
    );

    g_audioInitialized = true;
    return true;
}

void audio_exit(void) {
    if (!g_audioInitialized) return;

    g_stopRequested = true;
    g_playing       = false;

    g_decodeRunning = false;
    if (g_decodeThread) {
        threadJoin(g_decodeThread, U64_MAX);
        threadFree(g_decodeThread);
        g_decodeThread = 0;
    }

    decoder_close();

    ndspSetCallback(NULL, NULL);
    ndspChnReset(AUDIO_CHANNEL);
    ndspExit();

    g_audioInitialized = false;
}

static AudioType detect_type_from_extension(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return AUDIO_TYPE_NONE;
    dot++;

    if (!strcasecmp(dot, "mp3"))  return AUDIO_TYPE_MP3;
    if (!strcasecmp(dot, "ogg"))  return AUDIO_TYPE_OGG;
    if (!strcasecmp(dot, "flac")) return AUDIO_TYPE_FLAC;
    if (!strcasecmp(dot, "wav"))  return AUDIO_TYPE_WAV;

    return AUDIO_TYPE_NONE;
}

bool audio_play_file(const char* path) {
    if (!g_audioInitialized && !audio_init()) return false;

    audio_stop();

    g_audioType = detect_type_from_extension(path);
    if (g_audioType == AUDIO_TYPE_NONE) {
        return false;
    }

    g_srcSampleRate = AUDIO_SAMPLE_RATE;
    g_srcChannels   = 2;

    switch (g_audioType) {
        case AUDIO_TYPE_MP3:
            g_mp3 = mp3_open(path, &g_srcSampleRate, &g_srcChannels);
            if (!g_mp3) return false;
            break;
        case AUDIO_TYPE_OGG:
            g_ogg = ogg_open(path, &g_srcSampleRate, &g_srcChannels);
            if (!g_ogg) return false;
            break;
        case AUDIO_TYPE_FLAC:
            g_flac = flac_open(path, &g_srcSampleRate, &g_srcChannels);
            if (!g_flac) return false;
            break;
        case AUDIO_TYPE_WAV:
            g_wav = wav_open(path, &g_srcSampleRate, &g_srcChannels);
            if (!g_wav) return false;
            break;
        default:
            return false;
    }

    // Reset ring buffer
    g_pcmWritePos = 0;
    g_pcmReadPos  = 0;

    g_pitchSemitones = 0;
    g_speedFP        = FP_ONE;
    update_ndsp_pitch_speed();

    g_stopRequested = false;
    g_paused        = false;
    g_playing       = true;

    // Prime NDSP buffers immediately
    for (int i = 0; i < NDSP_NUM_BUFFERS; i++) {
        refill_wavebuf(&g_waveBuf[i]);
        DSP_FlushDataCache(g_ndspBuffers[i], NDSP_BUFFER_SAMPLES * 2 * sizeof(int16_t));
        ndspChnWaveBufAdd(AUDIO_CHANNEL, &g_waveBuf[i]);
    }

    return true;
}

void audio_stop(void) {
    if (!g_audioInitialized) return;

    g_stopRequested = true;
    g_playing       = false;
    g_paused        = false;

    ndspChnReset(AUDIO_CHANNEL);
    decoder_close();

    // Clear ring buffer
    g_pcmWritePos = 0;
    g_pcmReadPos  = 0;
}

void audio_pause_toggle(void) {
    if (!g_playing) return;
    g_paused = !g_paused;
}

bool audio_is_playing(void) {
    return g_playing && !g_paused;
}

bool audio_is_paused(void) {
    return g_paused;
}

// Pitch: −12..+12 semitones
void audio_pitch_step(int deltaSemitones) {
    int newPitch = g_pitchSemitones + deltaSemitones;
    if (newPitch < -12) newPitch = -12;
    if (newPitch >  12) newPitch =  12;
    g_pitchSemitones = newPitch;
    update_ndsp_pitch_speed();
}

// Speed: multiply by factor (0.25×..4.0×)
void audio_speed_step(float factor) {
    float current = (float)g_speedFP / (float)FP_ONE;
    current *= factor;
    if (current < 0.25f) current = 0.25f;
    if (current > 4.0f)  current = 4.0f;
    g_speedFP = (s32)(current * (float)FP_ONE);
    update_ndsp_pitch_speed();
}

void audio_reset_pitch_speed(void) {
    g_pitchSemitones = 0;
    g_speedFP        = FP_ONE;
    update_ndsp_pitch_speed();
}
