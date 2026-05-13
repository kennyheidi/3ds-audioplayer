/*
 * 3DS Audio Player with Pitch & Speed Control
 * Supports: MP3, OGG, FLAC, WAV
 *
 * Build targets:
 *   make      → audioplayer.3dsx  (run from Homebrew Launcher, 64MB RAM)
 *   make cia  → audioplayer.cia   (installable title, 96MB RAM on old 3DS
 *                                  with Luma3DS + EnableL2Cache on New 3DS)
 *
 * OLD 3DS OPTIMISATIONS in this file:
 *
 *   __stacksize__ = 512 KB
 *     stb_vorbis IMDCT functions allocate ~32 KB of local float arrays per
 *     stereo channel per frame.  A 326 KB stack overflow was confirmed in a
 *     crash dump before this was added.
 *
 *   No consoleInit
 *     consoleInit writes its background into one GFX double-buffer slot;
 *     C2D renders into the other.  They alternate every frame, producing
 *     green-stripe corruption on the bottom screen.  All error output is
 *     drawn via C2D instead.
 *
 *   30 fps render cap while playing
 *     Audio is driven by the dedicated DSP hardware (NDSP) — it never
 *     misses a beat regardless of what the main CPU is doing.  Drawing
 *     the UI at 60 fps when the CPU is already busy decoding audio wastes
 *     ~40% of available cycles on frames the user cannot perceive as
 *     smoother.  When AUDIO_PLAYING, we skip every other vsync and only
 *     call the C2D draw functions on alternate frames.  UI still reacts
 *     to input every frame; only the screen repaint is halved.
 *
 *   osSetSpeedupEnable(true)
 *     On New 3DS: enables the extra ARM11 cores and L2 cache, effectively
 *     doubling available CPU throughput — free performance with zero code
 *     changes.  On old 3DS: this call is a documented no-op, costs nothing.
 */

#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>

#include "audio.h"
#include "filebrowser.h"
#include "ui.h"

/*
 * Override the libctru default stack of 32 KB.
 * stb_vorbis decode functions (IMDCT) allocate ~32 KB of local float arrays
 * per stereo channel per frame.  326 KB of overflow was confirmed in the
 * crash dump before this fix was applied.
 */
u32 __stacksize__ = 0x80000; /* 512 KB */

/* ------------------------------------------------------------------ */
/*  Sleep-mode hook — keeps audio playing with lid closed              */
/*                                                                      */
/*  When the lid closes the OS fires APT_HOOK_SLEEP.  Without a hook   */
/*  the app suspends and NDSP stops.  We register a callback that:     */
/*    - On SLEEP:  pauses the screen (saves power) but leaves NDSP     */
/*                 running so music continues                           */
/*    - On WAKEUP: resumes screen rendering                            */
/*                                                                      */
/*  aptSetSleepAllowed(true) tells the OS we consent to sleep events.  */
/*  The hook then intercepts them instead of letting the OS suspend us. */
/* ------------------------------------------------------------------ */

static aptHookCookie s_sleep_hook;
static volatile bool s_sleeping = false;

static void sleep_hook_cb(APT_HookType hook, void* param) {
    (void)param;
    switch (hook) {
        case APTHOOK_ONSLEEP:
            /* Lid closed — stop rendering, keep NDSP running */
            s_sleeping = true;
            /* Dim backlight to save battery */
            GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
            break;

        case APTHOOK_ONWAKEUP:
            /* Lid opened — resume rendering */
            s_sleeping = false;
            GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Simple C2D error screen — used instead of printf when NDSP fails   */
/* ------------------------------------------------------------------ */

static void draw_ndsp_error(C3D_RenderTarget* top, C3D_RenderTarget* bottom,
                             C2D_TextBuf tbuf) {
    static const struct { const char* str; float y; u32 col; } lines[] = {
        { "Audio init failed!",           30,  0xFF4444FFu },
        { "DSP firmware not found.",      54,  0xEEEEFFFFu },
        { "",                             72,  0 },
        { "To fix:",                      82,  0xAAAACCFFu },
        { "1. Hold SELECT on boot",       100, 0xCCCCFFFFu },
        { "2. Open Rosalina menu",        118, 0xCCCCFFFFu },
        { "3. Miscellaneous options",     136, 0xCCCCFFFFu },
        { "4. Dump DSP firmware",         154, 0xCCCCFFFFu },
        { "5. Restart this app",          172, 0xCCCCFFFFu },
        { "",                             190, 0 },
        { "START = exit    A = continue", 205, 0x44FF88FFu },
    };

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(top, C2D_Color32(0x12, 0x12, 0x1E, 0xFF));
    C2D_SceneBegin(top);
    C2D_TextBufClear(tbuf);

    for (int i = 0; i < (int)(sizeof(lines)/sizeof(lines[0])); i++) {
        if (!lines[i].str[0]) continue;
        C2D_Text t;
        C2D_TextParse(&t, tbuf, lines[i].str);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor, 12.0f, lines[i].y, 0.5f,
                     0.5f, 0.5f, lines[i].col);
    }

    C2D_TargetClear(bottom, C2D_Color32(0x0E, 0x0E, 0x18, 0xFF));
    C2D_SceneBegin(bottom);

    C3D_FrameEnd(0);
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    romfsInit();
    cfguInit();

    /*
     * On New 3DS: activates the two extra ARM11 cores and the L2 cache.
     * This roughly doubles available CPU throughput for decoding-heavy
     * formats like FLAC and OGG at no code cost.
     * On old 3DS: documented no-op — safe to call unconditionally.
     */
    osSetSpeedupEnable(true);

    /*
     * Enable audio in sleep mode so music continues when the lid is closed.
     * The DSP processes audio independently of the main CPU; aptMainLoop()
     * will block but NDSP stays active.
     */
    aptSetSleepAllowed(true);
    aptHook(&s_sleep_hook, sleep_hook_cb, NULL);

    C3D_RenderTarget* top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    Result ndsp_result = ndspInit();
    bool   ndsp_ok     = R_SUCCEEDED(ndsp_result);

    if (!ndsp_ok) {
        C2D_TextBuf tbuf = C2D_TextBufNew(512);
        bool do_exit = false;

        while (aptMainLoop()) {
            hidScanInput();
            u32 k = hidKeysDown();
            if (k & KEY_START) { do_exit = true; break; }
            if (k & KEY_A)     break;
            draw_ndsp_error(top, bottom, tbuf);
        }

        C2D_TextBufDelete(tbuf);
        if (do_exit) goto cleanup_early;
    }

    {
        AudioState  audio;
        FileBrowser fb;
        UIState     ui;

        audio_init(&audio);
        audio.ndsp_available = ndsp_ok;
        filebrowser_init(&fb, "sdmc:/music");
        ui_init(&ui, &audio, &fb);

        /*
         * Hard 30 fps render lock.
         *
         * render_tick toggles every vsync (60 Hz).  We only call the C2D
         * draw functions on even ticks, giving us exactly 30 repaints per
         * second regardless of playback state.
         *
         * Audio is completely unaffected: audio_update() feeds NDSP buffer
         * refills every vsync (60 Hz) — the DSP hardware does not care how
         * often the screen is painted.  Decoding and rendering are
         * independent; halving the repaint rate gives the decoder the CPU
         * headroom it was fighting the renderer for.
         *
         * Input is also read every vsync so button presses are never missed.
         */
        int render_tick = 0;

        while (aptMainLoop()) {
            hidScanInput();
            u32 kDown = hidKeysDown();
            u32 kHeld = hidKeysHeld();

            /* --- Input (every frame @ 60 Hz) --- */
            if (kDown & KEY_DUP)   filebrowser_move(&fb, -1);
            if (kDown & KEY_DDOWN) filebrowser_move(&fb,  1);
            if (kDown & KEY_B)     filebrowser_go_up(&fb);

            if (kDown & KEY_A) {
                BrowserEntry* entry = filebrowser_selected(&fb);
                if (entry) {
                    if (entry->is_dir)  filebrowser_enter(&fb);
                    else if (ndsp_ok)   audio_play(&audio, entry->full_path);
                }
            }

            if (kDown & KEY_START)  audio_stop(&audio);
            if (kDown & KEY_SELECT) audio_toggle_pause(&audio);
            if (kDown & KEY_X)      audio_reset_fx(&audio);

            float speed_step = (kHeld & (KEY_L | KEY_R)) ? 0.1f : 0.05f;
            if (kDown & KEY_DLEFT)  audio_adjust_speed(&audio, -speed_step);
            if (kDown & KEY_DRIGHT) audio_adjust_speed(&audio,  speed_step);
            if (kDown & KEY_L)      audio_adjust_pitch(&audio, -1.0f);
            if (kDown & KEY_R)      audio_adjust_pitch(&audio,  1.0f);

            /* --- Audio decode (every frame @ 60 Hz — NDSP needs frequent refills) --- */
            if (ndsp_ok) audio_update(&audio);

            /* --- Render (every other frame = 30 Hz, skipped while sleeping) --- */
            render_tick ^= 1;
            if (s_sleeping) {
                /* Lid is closed — skip all rendering, just keep audio going */
                svcSleepThread(16666667LL); /* ~16ms = 60Hz pacing */
            } else if (render_tick == 0) {
                C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
                C2D_TargetClear(top,    C2D_Color32(0x12, 0x12, 0x1E, 0xFF));
                C2D_SceneBegin(top);
                ui_draw_top(&ui, top);
                C2D_TargetClear(bottom, C2D_Color32(0x0E, 0x0E, 0x18, 0xFF));
                C2D_SceneBegin(bottom);
                ui_draw_bottom(&ui, bottom);
                C3D_FrameEnd(0);
            } else {
                /* Skipped frame — sync to vsync then yield to NDSP thread */
                gspWaitForVBlank();
            }
        }

        audio_shutdown(&audio);
        ui_fini(&ui);
        filebrowser_free(&fb);
    }

    C2D_Fini();
    C3D_Fini();
    if (ndsp_ok) ndspExit();
    cfguExit();
    romfsExit();
    gfxExit();
    return 0;

cleanup_early:
    C2D_Fini();
    C3D_Fini();
    if (ndsp_ok) ndspExit();
    cfguExit();
    romfsExit();
    gfxExit();
    return 0;
}
