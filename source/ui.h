#pragma once
#include <citro2d.h>
#include "audio.h"
#include "filebrowser.h"

/*
 * OLD 3DS OPTIMISATION:
 *   - shared_buf is a single C2D_TextBuf pre-allocated once and cleared each
 *     frame.  The original code called C2D_TextBufNew/Delete on every draw_text
 *     call (~20+ per frame), which caused ~1200 malloc/free per second and
 *     tanked the frame-rate on the slow ARM11 allocator.
 *   - wave[] is halved to 100 samples; bars are 4 px wide so the visualiser
 *     still fills all 400 px of the top screen while halving the draw-call count.
 */

#define WAVE_BARS 100   /* was 200 */

typedef struct {
    AudioState*  audio;
    FileBrowser* fb;

    /* Pre-allocated text buffer — reused every frame instead of alloc per call */
    C2D_TextBuf  shared_buf;

    /* Waveform visualizer data */
    float        wave[WAVE_BARS];

    /* Cached colors */
    u32          col_bg;
    u32          col_accent;
    u32          col_text;
    u32          col_dim;
    u32          col_sel;
    u32          col_dir;
    u32          col_bar;
} UIState;

void ui_init(UIState* ui, AudioState* audio, FileBrowser* fb);
void ui_draw_top(UIState* ui, C3D_RenderTarget* target);
void ui_draw_bottom(UIState* ui, C3D_RenderTarget* target);
/* Call when shutting down to free the TextBuf */
void ui_fini(UIState* ui);
