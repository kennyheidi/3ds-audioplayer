// audio.c – drop‑in replacement for audio.h, tuned for old 3DS
// Uses dr_mp3, dr_flac, dr_wav, stb_vorbis with double‑buffered NDSP playback.

#include <3ds.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "audio.h"

// ---- dr_libs / stb_vorbis setup ----

#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"

// Minimal forward declarations for stb_vorbis (implementation is in vendor/stb_vorbis.c)
typedef struct stb_vorbis stb_vorbis;

typedef struct
{
    unsigned int sample_rate;
    int channels;
} stb_vorbis_info;

stb_vorbis*      stb_vorbis_open_filename(const char* path, int* error, const void* alloc_buffer);
void             stb_vorbis_close(stb_vorbis* f);
stb_vorbis_info  stb_vorbis_get_info(stb_vorbis* f);
unsigned int     stb_vorbis_stream_length_in_samples(stb_vorbis* f);
int              stb_vorbis_get_samples_short_interleaved(stb_vorbis* f, int channels, short* buffer, int num_shorts);

// ---- Internal decoder union ----

typedef struct {
    AudioFormat fmt;
    int         sampleRate;
    int         channels;
    u64         totalFrames;   // for duration
    u64         playedFrames;  // for position

    union {
        drmp3*      mp3;
        drflac*     flac;
        drwav*      wav;
        stb_vorbis* ogg;
        void*       raw;
    } h;
} DecoderState;

// ---- Helpers ----

static DecoderState* get_dec(AudioState* a) {
    return (DecoderState*)a->decoder;
}

static void decoder_close(AudioState* a) {
    DecoderState* d = get_dec(a);
    if (!d) return;

    switch (d->fmt) {
        case FMT_MP3:
            if (d->h.mp3) drmp3_close(d->h.mp3);
            break;
        case FMT_FLAC:
            if (d->h.flac) drflac_close(d->h.flac);
            break;
        case FMT_WAV:
            if (d->h.wav) drwav_close(d->h.wav);
            break;
        case FMT_OGG:
            if (d->h.ogg) stb_vorbis_close(d->h.ogg);
            break;
        default:
            break;
    }

    free(d);
    a->decoder = NULL;
    a->format  = FMT_UNKNOWN;
}

static AudioFormat detect_format(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return FMT_UNKNOWN;
    dot++;

    if (!strcasecmp(dot, "mp3"))  return FMT_MP3;
    if (!strcasecmp(dot, "flac")) return FMT_FLAC;
    if (!strcasecmp(dot, "wav"))  return FMT_WAV;
    if (!strcasecmp(dot, "ogg"))  return FMT_OGG;

    return FMT_UNKNOWN;
}

static void update_ndsp_rate_and_volume(AudioState* a) {
    if (!a->ndsp_available) return;

    DecoderState* d = get_dec(a);
    int baseRate = (d && d->sampleRate > 0) ? d->sampleRate : SAMPLE_RATE;

    float pitchRatio = powf(2.0f, a->pitch / 12.0f);
    float speedRatio = a->speed;
    float rate       = (float)baseRate * pitchRatio * speedRatio;

    if (rate < 8000.0f)  rate = 8000.0f;
    if (rate > 96000.0f) rate = 96000.0f;

    ndspChnSetRate(AUDIO_CHANNEL, rate);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    float v = a->volume;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    mix[0] = v;
    mix[1] = v;
    ndspChnSetMix(AUDIO_CHANNEL, mix);
}

static int decoder_read_frames(AudioState* a, s16* out, int maxFrames) {
    DecoderState* d = get_dec(a);
    if (!d || maxFrames <= 0) return 0;

    int framesRead = 0;

    switch (d->fmt) {
        case FMT_MP3: {
            // dr_mp3 outputs frames as PCM16 via drmp3_read_pcm_frames_s16
            framesRead = (int)drmp3_read_pcm_frames_s16(d->h.mp3, maxFrames, out);
        } break;

        case FMT_FLAC: {
            framesRead = (int)drflac_read_pcm_frames_s16(d->h.flac, maxFrames, out);
        } break;

        case FMT_WAV: {
            framesRead = (int)drwav_read_pcm_frames_s16(d->h.wav, maxFrames, out);
        } break;

        case FMT_OGG: {
            int samples = stb_vorbis_get_samples_short_interleaved(
                d->h.ogg,
                d->channels,
                out,
                maxFrames * d->channels
            );
            framesRead = samples;
        } break;

        default:
            framesRead = 0;
            break;
    }

    if (framesRead > 0) {
        d->playedFrames += (u64)framesRead;
        a->position = (d->sampleRate > 0)
                      ? (double)d->playedFrames / (double)d->sampleRate
                      : 0.0;
    }

    return framesRead;
}

static void fill_buffer(AudioState* a, int bufIndex) {
    DecoderState* d = get_dec(a);
    if (!d) {
        a->wave_buf[bufIndex].nsamples = 0;
        return;
    }

    s16* dst = a->pcm_buf[bufIndex];
    int  maxFrames = BUFFER_SIZE; // already in frames (2 seconds at SAMPLE_RATE)

    int frames = decoder_read_frames(a, dst, maxFrames);
    if (frames <= 0) {
        a->wave_buf[bufIndex].nsamples = 0;
        return;
    }

    a->wave_buf[bufIndex].data_vaddr = dst;
    a->wave_buf[bufIndex].nsamples   = frames;
    a->wave_buf[bufIndex].looping    = false;
    a->wave_buf[bufIndex].status     = NDSP_WBUF_FREE;

    DSP_FlushDataCache(dst, frames * d->channels * sizeof(s16));
    ndspChnWaveBufAdd(AUDIO_CHANNEL, &a->wave_buf[bufIndex]);
}

// ---- Public API ----

void audio_init(AudioState* a) {
    memset(a, 0, sizeof(*a));

    a->status = AUDIO_STOPPED;
    a->format = FMT_UNKNOWN;
    a->pitch  = 0.0f;
    a->speed  = 1.0f;
    a->volume = 1.0f;
    a->duration = 0.0;
    a->position = 0.0;

    a->ndsp_available = (ndspInit() == 0);
    if (!a->ndsp_available)
        return;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(AUDIO_CHANNEL, SAMPLE_RATE);

    float mix[12];
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(AUDIO_CHANNEL, mix);

    // Allocate double buffers
    for (int i = 0; i < 2; i++) {
        a->pcm_buf[i] = (s16*)linearAlloc(BUFFER_SIZE * 2 * sizeof(s16)); // stereo
        memset(&a->wave_buf[i], 0, sizeof(ndspWaveBuf));
    }
    a->active_buf = 0;

    // Optional processing buffer (same size as one PCM buffer)
    a->process_buf_size = BUFFER_SIZE * 2;
    a->process_buf      = (s16*)linearAlloc(a->process_buf_size * sizeof(s16));

    update_ndsp_rate_and_volume(a);
}

void audio_shutdown(AudioState* a) {
    if (a->ndsp_available) {
        ndspChnReset(AUDIO_CHANNEL);
        ndspExit();
        a->ndsp_available = false;
    }

    decoder_close(a);

    for (int i = 0; i < 2; i++) {
        if (a->pcm_buf[i]) {
            linearFree(a->pcm_buf[i]);
            a->pcm_buf[i] = NULL;
        }
    }

    if (a->process_buf) {
        linearFree(a->process_buf);
        a->process_buf = NULL;
    }
}

void audio_stop(AudioState* a) {
    if (!a->ndsp_available)
        return;

    ndspChnReset(AUDIO_CHANNEL);
    decoder_close(a);

    a->status   = AUDIO_STOPPED;
    a->position = 0.0;
    a->duration = 0.0;
    a->current_file[0]  = '\0';
    a->current_title[0] = '\0';
}

void audio_play(AudioState* a, const char* path) {
    if (!a->ndsp_available || !path)
        return;

    audio_stop(a);

    AudioFormat fmt = detect_format(path);
    if (fmt == FMT_UNKNOWN)
        return;

    DecoderState* d = (DecoderState*)calloc(1, sizeof(DecoderState));
    if (!d) return;

    d->fmt         = fmt;
    d->sampleRate  = SAMPLE_RATE;
    d->channels    = 2;
    d->totalFrames = 0;
    d->playedFrames = 0;

    bool ok = false;

    switch (fmt) {
        case FMT_MP3: {
            drmp3* mp3 = (drmp3*)malloc(sizeof(drmp3));
            if (mp3 && drmp3_init_file(mp3, path, NULL)) {
                d->h.mp3      = mp3;
                d->sampleRate = (int)mp3->sampleRate;
                d->channels   = (int)mp3->channels;
                d->totalFrames = drmp3_get_pcm_frame_count(mp3);
                ok = true;
            } else {
                if (mp3) free(mp3);
            }
        } break;

        case FMT_FLAC: {
            drflac* flac = drflac_open_file(path, NULL);
            if (flac) {
                d->h.flac      = flac;
                d->sampleRate  = (int)flac->sampleRate;
                d->channels    = (int)flac->channels;
                d->totalFrames = flac->totalPCMFrameCount;
                ok = true;
            }
        } break;

        case FMT_WAV: {
            drwav* wav = drwav_open_file(path, NULL);
            if (wav) {
                d->h.wav       = wav;
                d->sampleRate  = (int)wav->sampleRate;
                d->channels    = (int)wav->channels;
                d->totalFrames = wav->totalPCMFrameCount;
                ok = true;
            }
        } break;

        case FMT_OGG: {
            int err = 0;
            stb_vorbis* ogg = stb_vorbis_open_filename(path, &err, NULL);
            if (ogg && err == 0) {
                stb_vorbis_info info = stb_vorbis_get_info(ogg);
                d->h.ogg       = ogg;
                d->sampleRate  = (int)info.sample_rate;
                d->channels    = (int)info.channels;
                d->totalFrames = stb_vorbis_stream_length_in_samples(ogg);
                ok = true;
            }
        } break;

        default:
            break;
    }

    if (!ok) {
        free(d);
        return;
    }

    a->decoder = d;
    a->format  = fmt;

    // Duration in seconds
    if (d->sampleRate > 0 && d->totalFrames > 0) {
        a->duration = (double)d->totalFrames / (double)d->sampleRate;
    } else {
        a->duration = 0.0;
    }
    a->position = 0.0;

    strncpy(a->current_file, path, sizeof(a->current_file) - 1);
    a->current_file[sizeof(a->current_file) - 1] = '\0';
    strncpy(a->current_title, path, sizeof(a->current_title) - 1);
    a->current_title[sizeof(a->current_title) - 1] = '\0';

    ndspChnReset(AUDIO_CHANNEL);
    update_ndsp_rate_and_volume(a);

    // Prime both buffers
    for (int i = 0; i < 2; i++) {
        memset(&a->wave_buf[i], 0, sizeof
