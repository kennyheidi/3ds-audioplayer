#include "filebrowser.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

static const char* AUDIO_EXTS[] = { ".mp3", ".ogg", ".flac", ".wav", NULL };

static bool is_audio(const char* name) {
    const char* ext = strrchr(name, '.');
    if (!ext) return false;
    for (int i = 0; AUDIO_EXTS[i]; i++) {
        if (strcasecmp(ext, AUDIO_EXTS[i]) == 0) return true;
    }
    return false;
}

static int entry_cmp(const void* a, const void* b) {
    const BrowserEntry* ea = (const BrowserEntry*)a;
    const BrowserEntry* eb = (const BrowserEntry*)b;
    // Directories first, then files
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

void filebrowser_load(FileBrowser* fb) {
    fb->count    = 0;
    fb->selected = 0;
    fb->scroll   = 0;

    DIR* dir = opendir(fb->cwd);
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) && fb->count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue; // skip hidden / . / ..

        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/%s", fb->cwd, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        bool is_dir = S_ISDIR(st.st_mode);
        if (!is_dir && !is_audio(ent->d_name)) continue;

        BrowserEntry* e = &fb->entries[fb->count++];
        strncpy(e->name,      ent->d_name, sizeof(e->name) - 1);
        strncpy(e->full_path, path,        sizeof(e->full_path) - 1);
        e->is_dir = is_dir;
        e->size   = is_dir ? 0 : st.st_size;
    }
    closedir(dir);

    qsort(fb->entries, fb->count, sizeof(BrowserEntry), entry_cmp);
}

void filebrowser_init(FileBrowser* fb, const char* start_path) {
    strncpy(fb->cwd, start_path, sizeof(fb->cwd) - 1);
    filebrowser_load(fb);
}

void filebrowser_free(FileBrowser* fb) {
    (void)fb; // nothing heap-allocated
}

void filebrowser_move(FileBrowser* fb, int delta) {
    fb->selected += delta;
    if (fb->selected < 0)           fb->selected = 0;
    if (fb->selected >= fb->count)  fb->selected = fb->count - 1;

    // Scroll viewport
    if (fb->selected < fb->scroll)
        fb->scroll = fb->selected;
    if (fb->selected >= fb->scroll + VISIBLE_ROWS)
        fb->scroll = fb->selected - VISIBLE_ROWS + 1;
}

void filebrowser_enter(FileBrowser* fb) {
    BrowserEntry* e = filebrowser_selected(fb);
    if (e && e->is_dir) {
        strncpy(fb->cwd, e->full_path, sizeof(fb->cwd) - 1);
        filebrowser_load(fb);
    }
}

void filebrowser_go_up(FileBrowser* fb) {
    // Strip last path component
    char* slash = strrchr(fb->cwd, '/');
    if (slash && slash != fb->cwd) {
        *slash = '\0';
    } else {
        strncpy(fb->cwd, "sdmc:/", sizeof(fb->cwd) - 1);
    }
    filebrowser_load(fb);
}

BrowserEntry* filebrowser_selected(FileBrowser* fb) {
    if (fb->count == 0) return NULL;
    return &fb->entries[fb->selected];
}
