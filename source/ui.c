/*
 * ui.c  —  Top & bottom screen rendering
 *
 * Top screen  (400x240):  Now Playing, waveform, pitch/speed meters
 * Bottom screen (320x240): File browser
 *
 * OLD 3DS OPTIMISATIONS applied here:
 *
 *   1. SHARED TextBuf (biggest win):
 *      The original draw_text() called C2D_TextBufNew(256) and
 *      C2D_TextBufDelete() on every text draw — that is ~20+ malloc/free
 *      cycles per frame, ~1200+ per second.  On the old 3DS's ARM11 memory
 *      allocator this is extremely slow and destroyed the frame rate.
 *      Fix: allocate ONE C2D_TextBuf (2048 glyphs) in ui_init(), clear it
 *      once at the start of each draw pass, and reuse it for every text item
 *      in that pass.  draw_text() now takes the UIState* to access it.
 *
 *   2. WAVEFORM bar count halved (200 → 100):
 *      Each bar is now 3 px wide instead of 1 px, still covering the full
 *      400 px top screen.  Halves the GPU draw-call count for the visualiser.
 *
 *   3. No logic changes — audio/filebrowser behaviour is identical.
 */

#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Enough glyph slots for all text drawn in one frame (top + bottom screen).
   Top screen:  ~350 chars worst-case (title + time + labels + hints)
   Bottom screen: ~500 chars worst-case (12 rows × ~40 chars + header)
   2048 is a comfortable ceiling with no per-frame allocation.            */
#define TEXT_BUF_GLYPHS 2048

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

#define COL32(r,g,b,a) C2D_Color32(r,g,b,a)
#define SCREEN_W_TOP   400
#define SCREEN_W_BOT   320
#define SCREEN_H       240
#define ROW_H           18

static void draw_rect(float x, float y, float w, float h, u32 col) {
    C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

/*
 * draw_text — uses the pre-allocated shared_buf from UIState.
 * The caller must have called C2D_TextBufClear(ui->shared_buf) once at the
 * start of the frame; individual calls here just parse into available slots.
 */
static void draw_text(UIState* ui, const char* str,
                      float x, float y, float sz, u32 col) {
    C2D_Text text;
    C2D_TextParse(&text, ui->shared_buf, str);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, sz, sz, col);
}

static void draw_bar(float x, float y, float w, float h,
                     float value, float min_v, float max_v,
                     u32 bg_col, u32 fill_col) {
    draw_rect(x, y, w, h, bg_col);
    float frac = (value - min_v) / (max_v - min_v);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    draw_rect(x, y, w * frac, h, fill_col);
}

/* ------------------------------------------------------------------ */
/*  Init / fini                                                         */
/* ------------------------------------------------------------------ */

void ui_init(UIState* ui, AudioState* audio, FileBrowser* fb) {
    memset(ui, 0, sizeof(*ui));
    ui->audio = audio;
    ui->fb    = fb;

    /* Allocate the shared text buffer ONCE — never freed until ui_fini() */
    ui->shared_buf = C2D_TextBufNew(TEXT_BUF_GLYPHS);

    ui->col_bg     = COL32(0x12, 0x12, 0x1E, 0xFF);
    ui->col_accent = COL32(0x7C, 0x3A, 0xFF, 0xFF); // purple
    ui->col_text   = COL32(0xEE, 0xEE, 0xFF, 0xFF);
    ui->col_dim    = COL32(0x88, 0x88, 0xAA, 0xFF);
    ui->col_sel    = COL32(0x2A, 0x1A, 0x55, 0xFF);
    ui->col_dir    = COL32(0x7C, 0xD0, 0xFF, 0xFF);
    ui->col_bar    = COL32(0x2A, 0x2A, 0x44, 0xFF);
}

void ui_fini(UIState* ui) {
    if (ui->shared_buf) {
        C2D_TextBufDelete(ui->shared_buf);
        ui->shared_buf = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Top screen                                                          */
/* ------------------------------------------------------------------ */

void ui_draw_top(UIState* ui, C3D_RenderTarget* target) {
    AudioState* a = ui->audio;

    /*
     * Clear the shared TextBuf once at the start of the top-screen pass.
     * Bottom screen will clear it again before its own pass.
     * This makes all subsequent draw_text() calls in this function free —
     * they just increment a pointer into the already-allocated slab.
     */
    C2D_TextBufClear(ui->shared_buf);

    /* -- Header bar -- */
    draw_rect(0, 0, SCREEN_W_TOP, 24, ui->col_accent);
    draw_text(ui, "3DS Audio Player", 8, 4, 0.55f, COL32(0xFF,0xFF,0xFF,0xFF));

    /* Status badge */
    const char* status_label = "STOPPED";
    u32         status_col   = ui->col_dim;
    if (a->status == AUDIO_PLAYING) { status_label = " PLAYING"; status_col = COL32(0x44,0xFF,0x88,0xFF); }
    if (a->status == AUDIO_PAUSED)  { status_label = "  PAUSED"; status_col = COL32(0xFF,0xCC,0x44,0xFF); }
    draw_text(ui, status_label, 310, 4, 0.50f, status_col);

    /* -- Track title -- */
    char title_str[300];
    if (a->status != AUDIO_STOPPED && a->current_title[0])
        snprintf(title_str, sizeof(title_str), "%s", a->current_title);
    else
        snprintf(title_str, sizeof(title_str), "No track loaded");
    draw_text(ui, title_str, 8, 30, 0.52f, ui->col_text);

    /* -- Progress bar -- */
    float prog = audio_progress(a);
    draw_rect(8, 52, SCREEN_W_TOP - 16, 6, ui->col_bar);
    draw_rect(8, 52, (SCREEN_W_TOP - 16) * prog, 6, ui->col_accent);
    int pos_s = (int)a->position;
    int dur_s = (int)a->duration;
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d:%02d / %d:%02d",
             pos_s/60, pos_s%60, dur_s/60, dur_s%60);
    draw_text(ui, time_str, 8, 62, 0.42f, ui->col_dim);

    /* -- Waveform visualizer --
     * WAVE_BARS = 100 (was 200).  Each bar is now 4 px wide (was 2 px)
     * so the visualiser still fills the full 400 px top-screen width.
     * This halves the draw-call count from 200 to 100 per frame.        */
    audio_get_waveform(a, ui->wave, WAVE_BARS);
    float wy = 100.0f; // center Y
    float wh = 36.0f;  // half-height
    draw_rect(0, wy - wh, SCREEN_W_TOP, wh * 2.0f, COL32(0x1A,0x1A,0x30,0xFF));
    for (int i = 0; i < WAVE_BARS; i++) {
        float h = fabsf(ui->wave[i]) * wh;
        if (h < 1.0f) h = 1.0f;
        u32 wc = (a->status == AUDIO_PLAYING)
               ? COL32(0x7C, 0x3A, 0xFF, 0xCC)
               : COL32(0x44, 0x44, 0x66, 0x88);
        /* 4 px wide bars with 0 px gap — bar_x = i * 4, width = 3 */
        draw_rect(i * 4.0f, wy - h, 3.0f, h * 2.0f, wc);
    }

    /* -- Pitch control -- */
    draw_text(ui, "PITCH", 8, 148, 0.45f, ui->col_dim);
    draw_bar(50, 150, 120, 10, a->pitch, PITCH_MIN, PITCH_MAX,
             ui->col_bar, COL32(0xFF, 0x77, 0x44, 0xFF));
    char pitch_str[16];
    snprintf(pitch_str, sizeof(pitch_str), "%+.0f st", a->pitch);
    draw_text(ui, pitch_str, 178, 148, 0.45f, COL32(0xFF,0x99,0x66,0xFF));
    draw_text(ui, "L / R buttons", 8, 162, 0.38f, ui->col_dim);

    /* -- Speed control -- */
    draw_text(ui, "SPEED", 220, 148, 0.45f, ui->col_dim);
    draw_bar(262, 150, 120, 10, a->speed, SPEED_MIN, SPEED_MAX,
             ui->col_bar, COL32(0x44, 0xCC, 0xFF, 0xFF));
    char speed_str[16];
    snprintf(speed_str, sizeof(speed_str), "%.2fx", a->speed);
    draw_text(ui, speed_str, 388, 148, 0.45f, COL32(0x88,0xEE,0xFF,0xFF));
    draw_text(ui, "Left / Right", 220, 162, 0.38f, ui->col_dim);

    /* -- Controls reminder -- */
    draw_rect(0, 192, SCREEN_W_TOP, 1, ui->col_bar);
    draw_text(ui, "A=Play  B=Back  Sel=Pause  Sta=Stop  X=Reset", 6, 197, 0.40f, ui->col_dim);
    draw_text(ui, "L/R=Pitch  Left/Right=Speed", 6, 213, 0.40f, ui->col_dim);
}

/* ------------------------------------------------------------------ */
/*  Bottom screen — File Browser                                        */
/* ------------------------------------------------------------------ */

void ui_draw_bottom(UIState* ui, C3D_RenderTarget* target) {
    FileBrowser* fb = ui->fb;

    /* New frame pass — clear the shared buffer for the bottom screen's text */
    C2D_TextBufClear(ui->shared_buf);

    /* Header */
    draw_rect(0, 0, SCREEN_W_BOT, 18, COL32(0x22, 0x22, 0x3A, 0xFF));
    const char* cwd_disp = fb->cwd;
    if (strlen(cwd_disp) > 38) cwd_disp = fb->cwd + strlen(fb->cwd) - 38;
    char cwd_buf[64];
    snprintf(cwd_buf, sizeof(cwd_buf), "%s", cwd_disp);
    draw_text(ui, cwd_buf, 4, 2, 0.40f, ui->col_dim);

    if (fb->count == 0) {
        draw_text(ui, "No audio files found.", 8, 40, 0.48f, ui->col_dim);
        draw_text(ui, "Put MP3/OGG/FLAC/WAV in", 8, 58, 0.42f, ui->col_dim);
        draw_text(ui, "/music on your SD card.", 8, 72, 0.42f, ui->col_dim);
        return;
    }

    /* File list */
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = fb->scroll + i;
        if (idx >= fb->count) break;

        float ry = 20 + i * ROW_H;
        BrowserEntry* e = &fb->entries[idx];
        bool selected = (idx == fb->selected);

        if (selected)
            draw_rect(0, ry, SCREEN_W_BOT, ROW_H - 1, ui->col_sel);

        const char* icon = e->is_dir ? "[D]" : "[F]";
        u32 icon_col = e->is_dir ? ui->col_dir : ui->col_dim;
        draw_text(ui, icon, 4, ry + 2, 0.42f, icon_col);

        char name_disp[40];
        if (strlen(e->name) > 36) {
            strncpy(name_disp, e->name, 33);
            strcpy(name_disp + 33, "...");
        } else {
            strncpy(name_disp, e->name, sizeof(name_disp) - 1);
            name_disp[sizeof(name_disp) - 1] = '\0';
        }
        u32 name_col = selected ? ui->col_text
                                : (e->is_dir ? ui->col_dir : COL32(0xCC,0xCC,0xEE,0xFF));
        draw_text(ui, name_disp, 28, ry + 2, 0.44f, name_col);
    }

    /* Scrollbar */
    if (fb->count > VISIBLE_ROWS) {
        float sb_x    = SCREEN_W_BOT - 5;
        float sb_h    = SCREEN_H - 20;
        float thumb   = sb_h * VISIBLE_ROWS / fb->count;
        float thumb_y = 20 + sb_h * fb->scroll / fb->count;
        draw_rect(sb_x, 20, 4, sb_h, ui->col_bar);
        draw_rect(sb_x, thumb_y, 4, thumb, ui->col_accent);
    }

    /* Item count */
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d/%d", fb->selected + 1, fb->count);
    draw_text(ui, count_str, SCREEN_W_BOT - 40, SCREEN_H - 14, 0.38f, ui->col_dim);
}
