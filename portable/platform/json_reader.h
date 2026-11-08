#ifndef PORTABLE_JSON_READER_H
#define PORTABLE_JSON_READER_H
#include <stddef.h>
#include <stdint.h>
typedef struct {const char *at,*end;} PortableJsonReader;
void portable_json_whitespace(PortableJsonReader *reader);
int portable_json_take(PortableJsonReader *reader,char token);
int portable_json_string(PortableJsonReader *reader,char *out,size_t capacity);
int portable_json_number(PortableJsonReader *reader,uint32_t *out);
int portable_json_boolean(PortableJsonReader *reader,uint32_t *out);
#endif
