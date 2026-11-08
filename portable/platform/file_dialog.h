#ifndef PORTABLE_FILE_DIALOG_H
#define PORTABLE_FILE_DIALOG_H
#include <stddef.h>

typedef struct SDL_Window SDL_Window;
typedef struct PortableFileDialog PortableFileDialog;
/* Single-file async chooser. The request owns copies of label/pattern, and its
 * callback never accesses application owners. NULL means unavailable/allocation
 * failure. Call poll/free on the main thread; free is safe before completion. */
PortableFileDialog *portable_file_dialog_choose(SDL_Window *window,
    const char *label,const char *pattern);
PortableFileDialog *portable_file_dialog_choose_folder(SDL_Window *window);
/* 0 pending, 1 selected, 2 cancelled, 3 failed. Selected paths are copied only
 * when the complete path fits (including NUL); failure leaves path untouched.
 * Completion is retained until free, so polling is repeatable. */
int portable_file_dialog_poll(PortableFileDialog *request,char *path,size_t capacity);
void portable_file_dialog_free(PortableFileDialog *request);
#endif
