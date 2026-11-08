#ifndef PORTABLE_TEXT_GLYPH_H
#define PORTABLE_TEXT_GLYPH_H
#include <stdint.h>
/* Zero is also the valid space glyph. JSON validates the character separately. */
static inline int portable_text_character(char character)
{
    switch(character){
#define TEXT_GLYPH(character,bits) case character: return 1;
#include "text_glyphs.inc"
#undef TEXT_GLYPH
    default: return 0;
    }
}
static inline uint16_t portable_text_glyph(char character)
{
    switch(character){
#define TEXT_GLYPH(character,bits) case character: return bits;
#include "text_glyphs.inc"
#undef TEXT_GLYPH
    default: return 0u;
    }
}
#endif
