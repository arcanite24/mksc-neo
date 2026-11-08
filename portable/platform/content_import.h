#ifndef PORTABLE_CONTENT_IMPORT_H
#define PORTABLE_CONTENT_IMPORT_H

/* Temporary source-format export CLI used by the pack builders. Returns -1
 * when argv does not select an import command; otherwise a process exit code. */
int portable_content_import_command(int argc, char **argv);

#endif
