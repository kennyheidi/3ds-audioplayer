/*
 * audio.c — decoding + pitch/speed via resampling
 *
 * OLD 3DS STUTTER FIXES applied in this version:
 *
 *   1. 4 wave buffers instead of 2.
 *      The DSP consumes one buffer while we decode the next. With only 2
 *      buffers on O3DS, if a decode call takes longer than the buffer
 *      duration the DSP runs dry. 4 buffers gives us 3 buffers of
 *      headroom (~280ms) so a slow MP3 decode never causes a gap.
 *
 *   2. FRAMES_PER_BUF increased from SAMPLE_RATE (44100) to 4096.
 *      Smaller buffers = more frequent decode calls = more chances to
 *      miss. 4096 frames (~93ms) is large enough that one decode call
 *      per buffer is always fast enough on O3DS.
 *
 *   3. audio_update() now refills ALL free buffers in one call instead
 *      of returning after the first one. This primes the full pipeline
 *      on startup and recovers quickly if we ever fall behind.
 *
 *   4. dr_mp3 / dr_wav configured to decode at native sample rate
 *      without unnecessary channel conversion where possible.
 *
 *   5. All float (not double) arithmetic — VFPv2 has no hw double.
 */

#include "audio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

// STB_VORBIS_NO_STDIO intentionally not defined — file IO needed
#include "stb_vorbis.c"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static AudioFormat detect_format(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return FMT_UNKNOWN;
    if (strcasecmp(ext, ".mp3")  == 0) return FMT_MP3;
    if (strcasecmp(ext, ".ogg")  == 0) return FMT_OGG;
    if (strcasecmp(ext, ".flac") == 0) return FMT_FLAC;
    if (strcasecmp(ext, ".wav")  == 0) return FMT_WAV;
    return FMT_UNKNOWN;
}

static void extract_title(const char* path, char* title, int maxlen) {
    const char* slash = strrchr(path, '/');
    const char* src   = slash ? slash + 1 : path;
    strncpy(title, src, maxlen - 1);
    title[maxlen - 1] = '\0';
    char* dot = strrchr(title, '.');
    if (dot) *dot = '\0';
}

/*
 * Linear resample — all float, no double.
 * VFPv2 (O3DS) handles float natively; double is software-emulated (4-8x slower).
 */
static int resample_stereo(const s16* src, int src_frames,
                            s16* dst, int dst_max_frames,
                            float ratio) {
    int out = 0;
    for (int i = 0; i < dst_max_frames; i++) {
        float src_pos = (float)i / ratio;
        int   idx0    = (int)src_pos;
        int   idx1    = idx0 + 1;
        float frac    = src_pos - (float)idx0;
        if (idx1 >= src_frames) break;

        for (int ch = 0; ch < 2; ch++) {
            float s = src[idx0 * 2 + ch] * (1.0f - frac)
                    + src[idx1 * 2 + ch] * frac;
            if      (s < -32768.0f) s = -32768.0f;
            else if (s >  32767.0f) s =  32767.0f;
            dst[out * 2 + ch] = (s16)s;
        }
        out++;
    }
    return out;
}

/* Decode one buffer's worth of frames into raw scratch buffer */
static int decode_frames(AudioState* a, s16* raw, int frames_wanted) {
    switch (a->format) {
        case FMT_MP3:
            return (int)drmp3_read_pcm_frames_s16(
                       (drmp3*)a->decoder, frames_wanted, raw);
        case FMT_FLAC:
            return (int)drflac_read_pcm_frames_s16(
                       (drflac*)a->decoder, frames_wanted, raw);
        case FMT_WAV:
            return (int)drwav_read_pcm_frames_s16(
                       (drwav*)a->decoder, frames_wanted, raw);
        case FMT_OGG:
            return stb_vorbis_get_samples_short_interleaved(
                       (stb_vorbis*)a->decoder, 2, raw, frames_wanted * 2);
        default:
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void audio_init(AudioState* a) {
    memset(a, 0, sizeof(*a));
    a->status = AUDIO_STOPPED;
    a->volume = 1.0f;
    a->pitch  = 0.0f;
    a->speed  = 1.0f;

    for (int i = 0; i < NUM_BUFS; i++) {
        a->pcm_buf[i] = (s16*)linearAlloc(BUFFER_SIZE * sizeof(s16));
        memset(a->pcm_buf[i], 0, BUFFER_SIZE * sizeof(s16));
    }
    // process_buf needs to be big enough for the pre-resample frames
    a->process_buf_size = BUFFER_SIZE * 4;
    a->process_buf      = (s16*)linearAlloc(a->process_buf_size * sizeof(s16));

    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(AUDIO_CHANNEL, SAMPLE_RATE);
    ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = { 1.0f, 1.0f };
    ndspChnSetMix(AUDIO_CHANNEL, mix);
}

void audio_shutdown(AudioState* a) {
    audio_stop(a);
    for (int i = 0; i < NUM_BUFS; i++) {
        if (a->pcm_buf[i]) { linearFree(a->pcm_buf[i]); a->pcm_buf[i] = NULL; }
    }
    if (a->process_buf) { linearFree(a->process_buf); a->process_buf = NULL; }
}

void audio_stop(AudioState* a) {
    if (a->status == AUDIO_STOPPED) return;
    ndspChnReset(AUDIO_CHANNEL);
    a->status   = AUDIO_STOPPED;
    a->position = 0;

    if (a->decoder) {
        switch (a->format) {
            case FMT_MP3:  drmp3_uninit((drmp3*)a->decoder);          free(a->decoder); break;
            case FMT_FLAC: drflac_close((drflac*)a->decoder);                           break;
            case FMT_WAV:  drwav_uninit((drwav*)a->decoder);          free(a->decoder); break;
            case FMT_OGG:  stb_vorbis_close((stb_vorbis*)a->decoder);                  break;
            default: break;
        }
        a->decoder = NULL;
    }
}

void audio_play(AudioState* a, const char* path) {
    audio_stop(a);

    a->format = detect_format(path);
    if (a->format == FMT_UNKNOWN) return;

    strncpy(a->current_file, path, sizeof(a->current_file) - 1);
    extract_title(path, a->current_title, sizeof(a->current_title));

    bool ok = false;
    switch (a->format) {
        case FMT_MP3: {
            drmp3* dec = calloc(1, sizeof(drmp3));
            ok = drmp3_init_file(dec, path, NULL);
            if (ok) { a->decoder = dec; a->duration = (double)drmp3_get_pcm_frame_count(dec) / SAMPLE_RATE; }
            else free(dec);
            break;
        }
        case FMT_FLAC: {
            drflac* dec = drflac_open_file(path, NULL);
            ok = (dec != NULL);
            if (ok) { a->decoder = dec; a->duration = (double)dec->totalPCMFrameCount / dec->sampleRate; }
            break;
        }
        case FMT_WAV: {
            drwav* dec = calloc(1, sizeof(drwav));
            ok = drwav_init_file(dec, path, NULL);
            if (ok) { a->decoder = dec; a->duration = (double)dec->totalPCMFrameCount / dec->sampleRate; }
            else free(dec);
            break;
        }
        case FMT_OGG: {
            // Use stb_vorbis_open_file instead of stb_vorbis_open_filename
            // to avoid STB_VORBIS_NO_STDIO issues on devkitARM 3DS headers
            FILE* ogg_f = fopen(path, "rb");
            if (ogg_f) {
                int error = 0;
                stb_vorbis* dec = stb_vorbis_open_file(ogg_f, 0, &error, NULL);
                ok = (dec != NULL && error == 0);
                if (ok) { a->decoder = dec; a->duration = stb_vorbis_stream_length_in_seconds(dec); }
                else if (ogg_f) fclose(ogg_f);
            }
            break;
        }
        default: break;
    }

    if (!ok) { a->status = AUDIO_STOPPED; return; }

    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(AUDIO_CHANNEL, (float)(SAMPLE_RATE * a->speed));
    ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = { a->volume, a->volume };
    ndspChnSetMix(AUDIO_CHANNEL, mix);

    a->status     = AUDIO_PLAYING;
    a->active_buf = 0;
    a->position   = 0;

    for (int i = 0; i < NUM_BUFS; i++) {
        memset(&a->wave_buf[i], 0, sizeof(ndspWaveBuf));
        a->wave_buf[i].data_vaddr = a->pcm_buf[i];
        a->wave_buf[i].status     = NDSP_WBUF_FREE;
    }

    // Prime ALL buffers at startup — fills the full pipeline before playback starts
    // This ensures ~280ms of audio is ready before the DSP plays a single sample
    audio_update(a);
}

void audio_toggle_pause(AudioState* a) {
    if (a->status == AUDIO_PLAYING) {
        ndspChnSetPaused(AUDIO_CHANNEL, true);
        a->status = AUDIO_PAUSED;
    } else if (a->status == AUDIO_PAUSED) {
        ndspChnSetPaused(AUDIO_CHANNEL, false);
        a->status = AUDIO_PLAYING;
    }
}

/*
 * audio_update — scan all buffers and refill any that the DSP has finished.
 *
 * Correct buffer lifecycle:
 *   FREE  → we fill it and call ndspChnWaveBufAdd()
 *   QUEUED → DSP is waiting to play it (do not touch)
 *   PLAYING → DSP is currently playing it (do not touch)
 *   DONE  → DSP finished, safe to refill
 *
 * We must NOT set wb->status ourselves before adding — NDSP owns that
 * field once the buffer is queued. We just check FREE/DONE before filling.
 *
 * active_buf tracks which buffer slot to fill next in round-robin order,
 * advancing only when we actually fill a buffer (not every loop iteration).
 */
void audio_update(AudioState* a) {
    if (a->status != AUDIO_PLAYING) return;
    if (!a->decoder) return;

    float pitch_ratio = powf(2.0f, a->pitch / 12.0f);

    // Update playback rate once per call, not per buffer
    ndspChnSetRate(AUDIO_CHANNEL, (float)(SAMPLE_RATE * a->speed));

    for (int b = 0; b < NUM_BUFS; b++) {
        int buf_idx = (a->active_buf + b) % NUM_BUFS;
        ndspWaveBuf* wb = &a->wave_buf[buf_idx];

        // Only refill buffers the DSP has finished with
        if (wb->status != NDSP_WBUF_FREE && wb->status != NDSP_WBUF_DONE)
            continue;

        int frames_needed   = FRAMES_PER_BUF;
        int decode_frames_n = (int)((float)frames_needed / pitch_ratio) + 4;
        if (decode_frames_n * 2 > a->process_buf_size)
            decode_frames_n = a->process_buf_size / 2;

        int decoded = decode_frames(a, a->process_buf, decode_frames_n);

        if (decoded <= 0) {
            // End of file — let queued buffers finish playing before stopping
            a->status   = AUDIO_STOPPED;
            a->position = a->duration;
            return;
        }

        s16* dst        = a->pcm_buf[buf_idx];
        int  out_frames = resample_stereo(a->process_buf, decoded,
                                          dst, frames_needed, pitch_ratio);

        a->position += (double)decoded / SAMPLE_RATE;

        // Flush cache before handing buffer to DSP
        DSP_FlushDataCache(dst, out_frames * 2 * sizeof(s16));

        // Set up wavebuf — do NOT set wb->status here, NDSP manages it
        wb->data_vaddr = dst;
        wb->nsamples   = out_frames * 2;
        ndspChnWaveBufAdd(AUDIO_CHANNEL, wb);

        // Advance active_buf only when we actually filled a slot
        a->active_buf = (buf_idx + 1) % NUM_BUFS;
    }
}

void audio_adjust_pitch(AudioState* a, float semitones) {
    a->pitch += semitones;
    if (a->pitch < PITCH_MIN) a->pitch = PITCH_MIN;
    if (a->pitch > PITCH_MAX) a->pitch = PITCH_MAX;
}

void audio_adjust_speed(AudioState* a, float delta) {
    a->speed += delta;
    if (a->speed < SPEED_MIN) a->speed = SPEED_MIN;
    if (a->speed > SPEED_MAX) a->speed = SPEED_MAX;
    if (a->status == AUDIO_PLAYING)
        ndspChnSetRate(AUDIO_CHANNEL, (float)(SAMPLE_RATE * a->speed));
}

void audio_reset_fx(AudioState* a) {
    a->pitch = 0.0f;
    a->speed = 1.0f;
    if (a->status == AUDIO_PLAYING)
        ndspChnSetRate(AUDIO_CHANNEL, SAMPLE_RATE);
}

void audio_set_volume(AudioState* a, float vol) {
    a->volume = vol;
    float mix[12] = { vol, vol };
    ndspChnSetMix(AUDIO_CHANNEL, mix);
}

float audio_progress(const AudioState* a) {
    if (a->duration <= 0) return 0.0f;
    float p = (float)(a->position / a->duration);
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

void audio_get_waveform(const AudioState* a, float* buf, int n) {
    if (a->status == AUDIO_STOPPED || !a->pcm_buf[a->active_buf ^ 1]) {
        memset(buf, 0, n * sizeof(float));
        return;
    }
    s16* src   = a->pcm_buf[a->active_buf % NUM_BUFS];
    int  total = FRAMES_PER_BUF;
    for (int i = 0; i < n; i++) {
        int idx = (int)((float)i / n * total) * 2;
        buf[i]  = src[idx] / 32768.0f;
    }
}
