#pragma once
#include <stdbool.h>

#define MAX_ENTRIES   512
#define MAX_PATH      512
#define VISIBLE_ROWS   12

typedef struct {
    char  name[256];
    char  full_path[MAX_PATH];
    bool  is_dir;
    long  size;
} BrowserEntry;

typedef struct {
    char         cwd[MAX_PATH];
    BrowserEntry entries[MAX_ENTRIES];
    int          count;
    int          selected;
    int          scroll;      // top visible row
} FileBrowser;

void          filebrowser_init(FileBrowser* fb, const char* start_path);
void          filebrowser_free(FileBrowser* fb);
void          filebrowser_load(FileBrowser* fb);
void          filebrowser_move(FileBrowser* fb, int delta);
void          filebrowser_enter(FileBrowser* fb);
void          filebrowser_go_up(FileBrowser* fb);
BrowserEntry* filebrowser_selected(FileBrowser* fb);
