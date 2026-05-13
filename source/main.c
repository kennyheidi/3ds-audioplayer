/*
 * 3DS Audio Player with Pitch & Speed Control
 * Supports: MP3, OGG, FLAC, WAV
 *
 * Changes for old 3DS:
 *   - __stacksize__ = 512 KB  (stb_vorbis IMDCT uses huge local arrays)
 *   - No consoleInit: it conflicts with C2D on the same screen — the console
 *     writes its background to one double-buffer slot, C2D renders to the
 *     other, and they alternate visibly as green corruption on the bottom screen.
 *     NDSP errors are shown via C2D text instead of printf.
 *   - Render targets created before NDSP check so C2D is available for
 *     the error screen without restructuring the flow.
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

    /*
     * DO NOT call consoleInit here.
     *
     * consoleInit writes a solid background colour into one GFX double-buffer
     * slot.  C2D renders into the other slot.  The two alternate on every
     * gfxSwapBuffers() call, producing the green-stripe corruption visible on
     * the bottom screen.  All debug output now goes through C2D instead.
     */

    romfsInit();
    cfguInit();

    /*
     * Create render targets BEFORE the NDSP check so we can use C2D
     * to draw the error screen without restructuring the code flow.
     */
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

        while (aptMainLoop()) {
            hidScanInput();
            u32 kDown = hidKeysDown();
            u32 kHeld = hidKeysHeld();

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

            if (ndsp_ok) audio_update(&audio);

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top,    C2D_Color32(0x12, 0x12, 0x1E, 0xFF));
            C2D_SceneBegin(top);
            ui_draw_top(&ui, top);
            C2D_TargetClear(bottom, C2D_Color32(0x0E, 0x0E, 0x18, 0xFF));
            C2D_SceneBegin(bottom);
            ui_draw_bottom(&ui, bottom);
            C3D_FrameEnd(0);
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
