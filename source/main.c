/*
 * 3DS Audio Player with Pitch & Speed Control
 * Supports: MP3, OGG, FLAC, WAV
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
 * stb_vorbis IMDCT decode functions allocate large local float arrays
 * (~32KB per channel per frame). The default .3dsx stack of 32KB is not
 * nearly enough. This overrides the libctru weak symbol to give 512KB.
 */
u32 __stacksize__ = 0x80000;

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    PrintConsole console;
    consoleInit(GFX_BOTTOM, &console);

    romfsInit();
    cfguInit();

    printf("Initializing audio...\n");
    Result ndsp_result = ndspInit();
    bool ndsp_ok = R_SUCCEEDED(ndsp_result);

    if (!ndsp_ok) {
        printf("\x1b[31mAudio init failed!\x1b[0m\n");
        printf("Error: 0x%08lX\n\n", ndsp_result);
        printf("To fix:\n");
        printf("1. Hold SELECT on boot\n");
        printf("2. Rosalina menu\n");
        printf("3. Miscellaneous options\n");
        printf("4. Dump DSP firmware\n");
        printf("5. Restart app\n\n");
        printf("START=exit  A=continue\n");

        while (aptMainLoop()) {
            gfxSwapBuffers();
            gfxFlushBuffers();
            hidScanInput();
            u32 k = hidKeysDown();
            if (k & KEY_START) goto cleanup_early;
            if (k & KEY_A)     break;
        }
    } else {
        printf("Audio OK!\n");
    }

    {
        AudioState audio;
        FileBrowser fb;
        UIState ui;

        audio_init(&audio);
        audio.ndsp_available = ndsp_ok;
        filebrowser_init(&fb, "sdmc:/music");
        ui_init(&ui, &audio, &fb);

        C3D_RenderTarget* top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
        C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

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
        C2D_Fini();
        C3D_Fini();
    }

    if (ndsp_ok) ndspExit();
    cfguExit();
    romfsInit();
    gfxExit();
    return 0;

cleanup_early:
    if (ndsp_ok) ndspExit();
    cfguExit();
    romfsExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
