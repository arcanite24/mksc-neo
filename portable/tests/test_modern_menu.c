#include "menu_names.h"
#include "graphics_options.h"
#include "modern_menu.h"
#include "character_menu_renderer.h"
#include "ui_canvas.h"
#include "ui_font.h"
#include "ui_draw.h"
#include "ui_draw_list.h"
#include "mod_manager.h"
#include "mod_menu_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"modern menu failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

typedef struct
{
    unsigned counts[UI_DRAW_CHECKER + 1], calls, fail_at;
    UiDrawCommand last;
    float glyph_x[6];
    uint32_t glyph_color[6];
} DrawProbe;

static int probe_draw(void *owner, const UiDrawCommand *command)
{
    DrawProbe *probe = owner;
    CHECK(command->clip_x0 >= 0 && command->clip_x1 >= command->clip_x0);
    CHECK(command->clip_y0 >= 0 && command->clip_y1 >= command->clip_y0);
    if (command->kind == UI_DRAW_GLYPH && probe->counts[UI_DRAW_GLYPH] < 6u)
    {
        const unsigned i = probe->counts[UI_DRAW_GLYPH];
        CHECK(command->data.glyph.field != NULL && command->data.glyph.width > 0);
        probe->glyph_x[i] = command->data.glyph.x;
        probe->glyph_color[i] = command->data.glyph.color;
    }
    ++probe->counts[command->kind];
    probe->last = *command;
    ++probe->calls;
    return probe->fail_at == 0u || probe->calls < probe->fail_at;
}

static void recorded_presentation(void)
{
    UiCanvas canvas;
    DrawProbe probe = {0};
    UiCanvasSink sink = {.owner = &probe, .emit = probe_draw};
    ui_canvas_init(&canvas, NULL, 1280, 720, 1280);
    ui_canvas_bind_sink(&canvas, &sink);
    ui_canvas_clip(&canvas, 17, 23, 1000, 650);
    ui_fill_rect(&canvas, 1, 2, 3, 4, 0xff123456u, 0.5f);
    CHECK(probe.last.kind == UI_DRAW_RECT && probe.last.clip_x0 == 17 &&
          probe.last.clip_y1 == 650 && !probe.last.premultiplied_alpha);
    ui_canvas_reset_mode(&canvas, 1);
    ui_canvas_clear(&canvas, 0u);
    CHECK(canvas.sink == &sink && probe.last.premultiplied_alpha && probe.last.clip_x0 == 0);
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, 32, 0xfff0e0d0u);
    style.shadow_color = 0x80010203u;
    style.shadow_x = 3;
    style.outline = 2;
    style.outline_color = 0xff203040u;
    ui_text_draw(&canvas, &style, 25.25f, 100.5f, UI_ALIGN_LEFT, "AV", 1);
    CHECK(probe.counts[UI_DRAW_GLYPH] == 6u);
    CHECK(probe.glyph_color[0] == style.shadow_color && probe.glyph_color[1] == style.shadow_color);
    CHECK(probe.glyph_color[2] == style.outline_color && probe.glyph_color[3] == style.outline_color);
    CHECK(probe.glyph_color[4] == style.color && probe.glyph_color[5] == style.color);
    CHECK(fabsf(probe.glyph_x[0] - probe.glyph_x[2] - 3.0f) < 0.001f);
    CHECK(probe.glyph_x[3] > probe.glyph_x[2] && probe.glyph_x[3] == probe.glyph_x[5]);

    ModernMenu *menu = modern_menu_create();
    CHECK(menu != NULL);
    ModernMenuPause pause = {.external_background = 1u, .subtitle = "3D test course",
        .item_count = 2u, .labels = {"Continue", "Quit"},
        .kinds = {MODERN_PAUSE_CONTINUE, MODERN_PAUSE_QUIT}};
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    pause.tick = 64u;
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    CHECK(!sink.failed && probe.counts[UI_DRAW_ROUND_RECT] > 0u &&
          probe.counts[UI_DRAW_CONVEX] > 0u && probe.counts[UI_DRAW_GLYPH] > 6u);
    /* The pause Graphics page replaces the list with the shared option rows. */
    {
        PortablePauseGraphics page = {.open = 1u, .cursor = 6u};
        PortableSettings settings;
        portable_settings_defaults(&settings);
        portable_graphics_fill(&page, &settings);
        ModernMenuOptions rows;
        portable_graphics_modern_options(&rows, &page);
        CHECK(rows.row_count == PORTABLE_PAUSE_GRAPHICS_ROWS && rows.page == MODERN_MENU_OPTIONS_PAUSE_GRAPHICS);
        CHECK(!strcmp(rows.rows[6].label, "Internal Scale") && rows.rows[6].value[0] != '\0');
        CHECK(!strcmp(rows.rows[3].value, "Nearest") || !strcmp(rows.rows[3].value, "Linear"));
        pause.graphics = &rows;
        probe.counts[UI_DRAW_GLYPH] = 0u;
        CHECK(modern_menu_render_pause(menu, &pause, &canvas));
        CHECK(!sink.failed && probe.counts[UI_DRAW_GLYPH] > 40u);
        pause.graphics = NULL;
    }
    /* A bounded recorder cannot accidentally fall through to null CPU pixels
     * after it runs out of space, including during a complete overlay render. */
    probe.fail_at = probe.calls + 1u;
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    const unsigned stopped = probe.calls;
    CHECK(sink.failed);
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    CHECK(probe.calls == stopped);
    ui_canvas_init(&canvas, NULL, 1280, 720, 1280);
    CHECK(canvas.sink == NULL && !canvas.premultiplied_alpha);
    modern_menu_destroy(menu);
}

static void owned_draw_commands(void)
{
    CHECK(ui_draw_list_create(0u, 1024u) == NULL);
    CHECK(ui_draw_list_create(4097u, 1024u) == NULL);
    CHECK(ui_draw_list_create(1u, 16u * 1024u * 1024u + 1u) == NULL);
    CHECK(ui_draw_list_create(4096u, 1u) == NULL);
    UiDrawList *list = ui_draw_list_create(4096u, 4u * 1024u * 1024u);
    CHECK(list != NULL);
    UiCanvas canvas;
    ui_canvas_init(&canvas, NULL, 1280, 720, 1280);
    ui_canvas_bind_sink(&canvas, ui_draw_list_sink(list));
    uint32_t *pixels = malloc(4u * sizeof *pixels);
    CHECK(pixels != NULL);
    for (unsigned i = 0u; i < 4u; ++i)
        pixels[i] = 0xff112233u + i;
    UiImage image = {pixels, 2, 2};
    ui_draw_image(&canvas, &image, 0, 0, 20, 20, 1, UI_IMAGE_NEAREST, 0xffffffffu, 0);
    const size_t once = ui_draw_list_bytes(list);
    ui_draw_image(&canvas, &image, 20, 0, 20, 20, 1, UI_IMAGE_NEAREST, 0xffffffffu, 0);
    CHECK(ui_draw_list_bytes(list) == once);
    /* Reusing the exact source address after an Art eviction must record new
     * content rather than return the previous cached texture. */
    pixels[0] = 0xffabcdefu;
    ui_draw_image(&canvas, &image, 40, 0, 20, 20, 1, UI_IMAGE_NEAREST, 0xffffffffu, 0);
    CHECK(ui_draw_list_bytes(list) > once);
    free(pixels);
    memset(&image, 0, sizeof image);
    const UiDrawCommand *first = ui_draw_list_command(list, 0u);
    const UiDrawCommand *second = ui_draw_list_command(list, 1u);
    const UiDrawCommand *third = ui_draw_list_command(list, 2u);
    CHECK(first && second && third);
    CHECK(first->data.image.source == second->data.image.source);
    CHECK(first->data.image.source->pixels[0] == 0xff112233u);
    CHECK(third->data.image.source->pixels[0] == 0xffabcdefu);
    CHECK(first->data.image.source->pixels[3] == 0xff112236u);
    UiPoint points[3] = {{1, 2}, {12, 3}, {8, 15}};
    UiPaint paint = ui_paint_solid(0xffffffffu);
    ui_fill_convex(&canvas, points, 3, &paint, 1, 1);
    points[0].x = 999;
    CHECK(ui_draw_list_command(list, 3u)->data.convex.points[0].x == 1);

    UiTextStyle style = ui_text_style(UI_FONT_BLACK, 32, 0xfff0e0d0u);
    ui_text_draw(&canvas, &style, 25, 100, UI_ALIGN_LEFT, "AA", 1);
    const UiDrawCommand *glyph = ui_draw_list_command(list, 4u);
    CHECK(glyph && glyph->kind == UI_DRAW_GLYPH);
    CHECK(ui_draw_list_command(list, 5u)->data.glyph.field == glyph->data.glyph.field);
    const size_t field_size = (size_t)glyph->data.glyph.width * (size_t)glyph->data.glyph.height;
    unsigned char *field_copy = malloc(field_size);
    CHECK(field_copy != NULL);
    memcpy(field_copy, glyph->data.glyph.field, field_size);
    ui_font_release();
    CHECK(memcmp(field_copy, glyph->data.glyph.field, field_size) == 0);
    free(field_copy);
    CHECK(!ui_draw_list_sink(list)->failed);
    ui_draw_list_reset(list);
    ModernMenu *menu = modern_menu_create();
    CHECK(menu != NULL);
    ModernMenuPause pause = {.external_background = 1u,
                             .subtitle = "Owned queued pause",
                             .item_count = 2u,
                             .labels = {"Continue", "Quit"},
                             .kinds = {MODERN_PAUSE_CONTINUE, MODERN_PAUSE_QUIT}};
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    pause.tick = 64u;
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    CHECK(!ui_draw_list_sink(list)->failed && ui_draw_list_count(list) > 20u);
    CHECK(ui_draw_list_bytes(list) <= 4u * 1024u * 1024u);
    modern_menu_destroy(menu);
    ui_draw_list_close(list);

    list = ui_draw_list_create(1u, 4096u);
    CHECK(list != NULL);
    ui_canvas_bind_sink(&canvas, ui_draw_list_sink(list));
    ui_canvas_clear(&canvas, 0u);
    ui_fill_rect(&canvas, 0, 0, 1, 1, 0xffffffffu, 1);
    CHECK(ui_draw_list_sink(list)->failed && ui_draw_list_error(list)[0]);
    CHECK(ui_draw_list_count(list) == 0u && ui_draw_list_command(list, 0u) == NULL);
    const size_t stopped = ui_draw_list_bytes(list);
    ui_canvas_clear(&canvas, 0u);
    CHECK(ui_draw_list_bytes(list) == stopped);
    ui_draw_list_reset(list);
    CHECK(!ui_draw_list_sink(list)->failed && !ui_draw_list_error(list)[0]);
    ui_canvas_clear(&canvas, 0u);
    CHECK(ui_draw_list_count(list) == 1u);
    ui_draw_list_reset(list);
    uint32_t oversized[1024] = {0};
    image = (UiImage){oversized, 32, 32};
    ui_draw_image(&canvas, &image, 0, 0, 32, 32, 1, UI_IMAGE_NEAREST, 0xffffffffu, 0);
    CHECK(ui_draw_list_sink(list)->failed && ui_draw_list_count(list) == 0u);
    CHECK(ui_draw_list_bytes(list) <= 4096u);
    ui_draw_list_reset(list);
    UiDrawCommand invalid = {.kind = UI_DRAW_CONVEX};
    CHECK(ui_canvas_emit(&canvas, &invalid));
    CHECK(ui_draw_list_sink(list)->failed && ui_draw_list_error(list)[0]);
    ui_draw_list_reset(list);
    invalid = (UiDrawCommand){.kind = UI_DRAW_RECT, .data.rect = {NAN, 0, 1, 1, 1, 0xffffffffu}};
    CHECK(ui_canvas_emit(&canvas, &invalid));
    CHECK(ui_draw_list_sink(list)->failed && ui_draw_list_count(list) == 0u);
    ui_draw_list_close(list);
}

static void recorded_large_source_mutation(void)
{
    enum
    {
        WIDTH = 256,
        HEIGHT = 256
    };
    UiDrawList *list = ui_draw_list_create(8u, 1024u * 1024u);
    CHECK(list != NULL);
    uint32_t *pixels = malloc((size_t)WIDTH * HEIGHT * sizeof *pixels);
    CHECK(pixels != NULL);
    for (size_t i = 0; i < (size_t)WIDTH * HEIGHT; ++i)
        pixels[i] = 0xff123456u;
    UiImage source = {pixels, WIDTH, HEIGHT};
    UiCanvas canvas;
    ui_canvas_init(&canvas, NULL, WIDTH, HEIGHT, WIDTH);
    ui_canvas_bind_sink(&canvas, ui_draw_list_sink(list));
    ui_draw_image(&canvas, &source, 0, 0, WIDTH, HEIGHT, 1, UI_IMAGE_NEAREST, UINT32_MAX, 0);
    const size_t copied = ui_draw_list_bytes(list);
    for (unsigned i = 0; i < 3u; ++i)
        ui_draw_image(&canvas, &source, 0, 0, WIDTH, HEIGHT, 1, UI_IMAGE_NEAREST, UINT32_MAX, 0);
    CHECK(ui_draw_list_count(list) == 4u && ui_draw_list_bytes(list) == copied);
    const UiImage *original = ui_draw_list_command(list, 0u)->data.image.source;
    for (size_t i = 1; i < 4u; ++i)
        CHECK(ui_draw_list_command(list, i)->data.image.source == original);
    /* Equality must cover the complete source, not its address or prefix. */
    pixels[(size_t)WIDTH * HEIGHT - 1u] = 0xffabcdefu;
    ui_draw_image(&canvas, &source, 0, 0, WIDTH, HEIGHT, 1, UI_IMAGE_NEAREST, UINT32_MAX, 0);
    CHECK(!ui_draw_list_sink(list)->failed && ui_draw_list_count(list) == 5u);
    CHECK(ui_draw_list_bytes(list) > copied && ui_draw_list_bytes(list) <= 1024u * 1024u);
    const UiImage *changed = ui_draw_list_command(list, 4u)->data.image.source;
    CHECK(changed != original && original->pixels[(size_t)WIDTH * HEIGHT - 1u] == 0xff123456u);
    CHECK(changed->pixels[(size_t)WIDTH * HEIGHT - 1u] == 0xffabcdefu);
    CHECK(original->pixels[0] == changed->pixels[0]);
    free(pixels);
    memset(&source, 0, sizeof source);
    CHECK(original->pixels[0] == 0xff123456u && changed->pixels[0] == 0xff123456u);
    ui_draw_list_reset(list);
    CHECK(ui_draw_list_count(list) == 0u && !ui_draw_list_sink(list)->failed);
    ui_draw_list_close(list);
}

static size_t changed_pixels(const uint32_t *before, const uint32_t *after, int width, int height,
                             int stride, int left, int top, int right, int bottom)
{
    size_t changed = 0u;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (before[y * stride + x] != after[y * stride + x])
            {
                CHECK(x >= left && x < right && y >= top && y < bottom);
                ++changed;
            }
    return changed;
}

static int render_results_immutable(ModernMenu *menu,const ModernMenuResults *results,UiCanvas *canvas)
{
    ModernMenuResults before;memcpy(&before,results,sizeof before);
    const int rendered=modern_menu_render_results(menu,results,canvas);
    CHECK(!memcmp(&before,results,sizeof before));
    return rendered;
}

static void modern_results_pixels(void)
{
    enum { RACE_W=24,RACE_H=16 };
    uint32_t race[RACE_W*RACE_H],race_before[RACE_W*RACE_H];
    for(unsigned i=0u;i<RACE_W*RACE_H;++i)race[i]=UINT32_C(0xff204060)+(i%24u)*256u;
    memcpy(race_before,race,sizeof race);
    uint8_t image[32u+16u*16u*4u]={0},image_before[sizeof image];
    memcpy(image,"MKSCIMG1",8u);image[8]=32u;image[10]=1u;image[12]=16u;image[14]=16u;image[24]=32u;
    image[28]=(uint8_t)(sizeof image & 0xffu);image[29]=(uint8_t)(sizeof image>>8u);
    for(unsigned i=0u;i<256u;++i){image[32u+i*4u]=0xfeu;image[33u+i*4u]=1u;
        image[34u+i*4u]=0xf3u;image[35u+i*4u]=0xffu;}
    memcpy(image_before,image,sizeof image);
    const PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
        .content_id=900001u,.version=1u,.bytes=image,.size=sizeof image};
    const PortableAssetPackView pack={.section_count=1u,.resolved_sections=&section};
    ModernMenuResults results={.frame=race,.frame_width=RACE_W,.frame_height=RACE_H,
        .title="Race results",.subtitle="Authored Cup / 150cc",.row_count=8u,.item_count=5u,
        .labels={"Retry","Watch replay","Save ghost","Change course","Quit"},.tick=64u};
    for(unsigned i=0u;i<8u;++i){
        results.rows[i].driver_id=i+1u;results.rows[i].rank=i+1u;
        results.rows[i].highlighted=(uint8_t)(i==7u);
        snprintf(results.rows[i].value,sizeof results.rows[i].value,"%u:12.34",i+1u);
        snprintf(results.rows[i].detail,sizeof results.rows[i].detail,"Lap %u / personal best",i+1u);
    }
    results.rows[6].driver_id=900u; /* Missing custom names/art have an ordinary fallback. */
    results.rows[6].portrait_id=900002u;
    for(unsigned wide=0u;wide<2u;++wide)for(unsigned enlarged=0u;enlarged<2u;++enlarged){
        const int scale=enlarged?4:1,width=(wide?373:320)*scale,height=(wide?210:240)*scale,stride=width+7;
        const size_t bytes=(size_t)stride*(size_t)height*sizeof(uint32_t);
        uint32_t *pixels=malloc(bytes),*baseline=malloc(bytes);CHECK(pixels && baseline);
        UiCanvas canvas;ui_canvas_init(&canvas,pixels,width,height,stride);
        ModernMenu *menu=modern_menu_create();CHECK(menu!=NULL);
        results.frame_w=(float)width;results.frame_h=(float)height;
        results.cursor=0u;results.pack=NULL;results.notice=NULL;results.gamepad=0u;
        for(size_t i=0u;i<bytes/sizeof *pixels;++i)pixels[i]=UINT32_C(0xabcdef01);
        results.tick=0u;CHECK(render_results_immutable(menu,&results,&canvas));results.tick=64u;
        ModernMenuResults before;memcpy(&before,&results,sizeof before);
        CHECK(render_results_immutable(menu,&results,&canvas));memcpy(baseline,pixels,bytes);
        CHECK(render_results_immutable(menu,&results,&canvas));CHECK(!memcmp(baseline,pixels,bytes));
        CHECK(!memcmp(&before,&results,sizeof results));
        /* Every row's primary number is visibly submitted, including row8.
         * Changing it cannot repaint the neighboring action column. */
        for(unsigned row=0u;row<8u;++row){
            char value[32];memcpy(value,results.rows[row].value,sizeof value);
            snprintf(results.rows[row].value,sizeof results.rows[row].value,"88:88.88");
            CHECK(render_results_immutable(menu,&results,&canvas));
            CHECK(changed_pixels(baseline,pixels,width,height,stride,0,height/10,width*7/10,height*9/10)>0u);
            memcpy(results.rows[row].value,value,sizeof value);
        }
        for(unsigned cursor=1u;cursor<5u;++cursor){
            results.cursor=cursor;CHECK(render_results_immutable(menu,&results,&canvas));
            CHECK(changed_pixels(baseline,pixels,width,height,stride,width/2,0,width,height)>0u);
        }
        results.cursor=0u;
        /* Times, cumulative points and Battle survivors use the same table
         * without hiding the last row or consuming game state. */
        for(unsigned mode=0u;mode<2u;++mode){
            for(unsigned row=0u;row<8u;++row)snprintf(results.rows[row].value,sizeof results.rows[row].value,
                mode?"%u balloons":"%u points",row+1u);
            CHECK(render_results_immutable(menu,&results,&canvas));
            CHECK(changed_pixels(baseline,pixels,width,height,stride,0,height/10,width*7/10,height*9/10)>0u);
        }
        for(unsigned row=0u;row<8u;++row)snprintf(results.rows[row].value,sizeof results.rows[row].value,"%u:12.34",row+1u);
        results.notice="Ghost saved. Choose a slot to replace.";
        CHECK(render_results_immutable(menu,&results,&canvas));
        CHECK(changed_pixels(baseline,pixels,width,height,stride,width/2,height/2,width,height)>0u);
        results.notice=NULL;
        results.disabled_mask=1u<<2u;
        CHECK(render_results_immutable(menu,&results,&canvas));
        CHECK(changed_pixels(baseline,pixels,width,height,stride,width/2,0,width,height)>0u);
        results.disabled_mask=0u;results.gamepad=1u;results.hint="Choose a ghost slot";
        CHECK(render_results_immutable(menu,&results,&canvas));
        CHECK(changed_pixels(baseline,pixels,width,height,stride,0,height/2,width,height)>0u);
        results.gamepad=0u;results.hint=NULL;
        results.pack=&pack;results.rows[6].portrait_id=900001u;
        CHECK(render_results_immutable(menu,&results,&canvas));
        CHECK(changed_pixels(baseline,pixels,width,height,stride,0,height/10,width*7/10,height*9/10)>0u);
        size_t custom=0u;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)custom+=pixels[y*stride+x]==UINT32_C(0xfff301fe);
        CHECK(custom>0u);
        /* Model a later pack reusing the same allocation/view addresses.
         * The content-generation boundary must retire the decoded portrait. */
        for(unsigned i=0u;i<256u;++i) image[32u+i*4u]^=0x80u;
        modern_menu_content_changed(menu);
        CHECK(render_results_immutable(menu,&results,&canvas));
        size_t revised=0u;custom=0u;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            custom+=pixels[y*stride+x]==UINT32_C(0xfff301fe);
            revised+=pixels[y*stride+x]==UINT32_C(0xfff3017e);
        }
        CHECK(revised>0u && custom==0u);
        memcpy(image,image_before,sizeof image);
        modern_menu_content_changed(menu);
        CHECK(render_results_immutable(menu,&results,&canvas));
        const uint32_t own_base=portable_character_roster_art(201u);
        PortableAssetSectionView own_sections[2]={section,section},fallback_sections[2]={section,section},portrait_section=section;
        own_sections[0].content_id=fallback_sections[0].content_id=portrait_section.content_id=
            own_base+PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT;
        own_sections[1].content_id=own_base+PORTABLE_CHARACTER_ROSTER_BOARD_NAME;
        fallback_sections[1].content_id=own_base+PORTABLE_CHARACTER_ROSTER_NAME;
        const PortableAssetPackView own_pack={.section_count=2u,.resolved_sections=own_sections};
        const PortableAssetPackView fallback_pack={.section_count=2u,.resolved_sections=fallback_sections};
        const PortableAssetPackView portrait_pack={.section_count=1u,.resolved_sections=&portrait_section};
        results.rows[6].driver_id=201u;results.rows[6].portrait_id=0u;results.pack=&own_pack;
        CHECK(render_results_immutable(menu,&results,&canvas));
        uint32_t *authored=malloc(bytes);CHECK(authored!=NULL);memcpy(authored,pixels,bytes);
        results.pack=&portrait_pack;CHECK(render_results_immutable(menu,&results,&canvas));
        CHECK(changed_pixels(authored,pixels,width,height,stride,0,height/10,width*7/10,height*9/10)>0u);
        results.pack=&fallback_pack;CHECK(render_results_immutable(menu,&results,&canvas));
        CHECK(!memcmp(authored,pixels,bytes)); /* Own menu-name art fills a missing board-name part. */
        free(authored);
        results.rows[6].driver_id=900u;results.rows[6].portrait_id=900002u;results.pack=&pack;
        CHECK(render_results_immutable(menu,&results,&canvas));CHECK(!memcmp(baseline,pixels,bytes));
        CHECK(!memcmp(race,race_before,sizeof race) && !memcmp(image,image_before,sizeof image));
        for(int y=0;y<height;++y)for(int x=width;x<stride;++x)CHECK(pixels[y*stride+x]==UINT32_C(0xabcdef01));
        /* Reject malformed views before clearing/dimming the caller's frame. */
        for(unsigned bad=0u;bad<9u;++bad){
            ModernMenuResults invalid=results;UiCanvas invalid_canvas=canvas;
            switch(bad){
            case 0u:invalid.row_count=9u;break;
            case 1u:invalid.item_count=6u;break;
            case 2u:invalid.cursor=5u;break;
            case 3u:invalid.frame=NULL;break;
            case 4u:invalid.frame_width=0;break;
            case 5u:invalid.frame_h=0.0f;break;
            case 6u:invalid.labels[2]=NULL;break;
            case 7u:invalid_canvas.width=0;break;
            default:invalid_canvas.pixels=NULL;break;
            }
            CHECK(!modern_menu_render_results(menu,&invalid,&invalid_canvas));CHECK(!memcmp(baseline,pixels,bytes));
        }
        CHECK(!modern_menu_render_results(NULL,&results,&canvas));
        CHECK(!modern_menu_render_results(menu,NULL,&canvas));CHECK(!modern_menu_render_results(menu,&results,NULL));
        results.row_count=results.item_count=0u;results.title=NULL;results.cursor=0u;
        CHECK(render_results_immutable(menu,&results,&canvas)); /* A passive wait screen is valid. */
        results.row_count=8u;results.item_count=5u;results.title="Race results";
        modern_menu_destroy(menu);free(baseline);free(pixels);
    }
}

static int render_transition_immutable(ModernMenu *menu,const ModernMenuTransition *transition,UiCanvas *canvas)
{
    ModernMenuTransition before;memcpy(&before,transition,sizeof before);
    const int rendered=modern_menu_render_transition(menu,transition,canvas);
    CHECK(!memcmp(&before,transition,sizeof before));
    return rendered;
}

static void modern_transition_pixels(void)
{
    const uint8_t unrelated_bytes[4]={1u,2u,3u,4u};
    const PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
        .content_id=900001u,.version=1u,.bytes=unrelated_bytes,.size=sizeof unrelated_bytes};
    const PortableAssetPackView sparse={.section_count=1u,.resolved_sections=&section};
    uint8_t pack_before[sizeof sparse],section_before[sizeof section];
    memcpy(pack_before,&sparse,sizeof sparse);memcpy(section_before,&section,sizeof section);
    for(unsigned wide=0u;wide<2u;++wide)for(unsigned enlarged=0u;enlarged<2u;++enlarged){
        const int scale=enlarged?4:1,width=(wide?373:320)*scale,height=(wide?210:240)*scale,stride=width+9;
        const size_t count=(size_t)width*(size_t)height,bytes=(size_t)stride*(size_t)height*sizeof(uint32_t);
        uint32_t *race=malloc(count*sizeof *race),*saved=malloc(count*sizeof *saved),
            *pixels=malloc(bytes),*before=malloc(bytes);CHECK(race && saved && pixels && before);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            race[(size_t)y*(size_t)width+(size_t)x]=UINT32_C(0xff000000)|
                ((uint32_t)(x&255)<<16u)|((uint32_t)(y&255)<<8u)|(uint32_t)((x+y)&255);
        memcpy(saved,race,count*sizeof *race);
        for(size_t i=0u;i<bytes/sizeof *pixels;++i)pixels[i]=UINT32_C(0xabcdef01);
        UiCanvas canvas;ui_canvas_init(&canvas,pixels,width,height,stride);
        ModernMenu *menu=modern_menu_create();CHECK(menu!=NULL);
        ModernMenuTransition transition={.frame=race,.frame_width=width,.frame_height=height,
            .frame_w=(float)width,.frame_h=(float)height,.subtitle="Authored Cup / 150cc",
            .course_id=900002u,.duration=100u};
        for(unsigned kind=0u;kind<4u;++kind){
            transition.kind=(ModernMenuTransitionKind)kind;transition.elapsed=0u;
            CHECK(render_transition_immutable(menu,&transition,&canvas));memcpy(before,pixels,bytes);
            transition.elapsed=50u;CHECK(render_transition_immutable(menu,&transition,&canvas));
            CHECK(changed_pixels(before,pixels,width,height,stride,0,0,width,height)>0u);
            transition.elapsed=80u;CHECK(render_transition_immutable(menu,&transition,&canvas));memcpy(before,pixels,bytes);
            CHECK(render_transition_immutable(menu,&transition,&canvas));CHECK(!memcmp(before,pixels,bytes));
            /* Progress belongs to the view: reaching age80 in a fresh renderer
             * must match age80 after earlier renders, with no hidden tick. */
            ModernMenu *fresh=modern_menu_create();CHECK(fresh!=NULL);
            CHECK(render_transition_immutable(fresh,&transition,&canvas));CHECK(!memcmp(before,pixels,bytes));
            modern_menu_destroy(fresh);
            transition.elapsed=100u;CHECK(render_transition_immutable(menu,&transition,&canvas));
            size_t revealed=0u;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                const uint32_t value=pixels[y*stride+x];
                revealed+=value==race[y*width+x];
                if(kind!=MODERN_TRANSITION_LOADING)CHECK(value==race[y*width+x]);
            }
            CHECK(kind==MODERN_TRANSITION_LOADING?revealed<count:revealed==count);
            memcpy(before,pixels,bytes);transition.elapsed=UINT32_MAX;
            CHECK(render_transition_immutable(menu,&transition,&canvas));CHECK(!memcmp(before,pixels,bytes));
            CHECK(!memcmp(race,saved,count*sizeof *race));
        }
        /* Sparse/mod packs without catalog art preserve a readable generic
         * course card rather than selecting an unrelated frontend image. */
        transition.kind=MODERN_TRANSITION_ENTRY;transition.elapsed=20u;
        CHECK(render_transition_immutable(menu,&transition,&canvas));memcpy(before,pixels,bytes);
        transition.pack=&sparse;CHECK(render_transition_immutable(menu,&transition,&canvas));
        CHECK(!memcmp(before,pixels,bytes));
        transition.course_id=0u;CHECK(render_transition_immutable(menu,&transition,&canvas));
        CHECK(changed_pixels(before,pixels,width,height,stride,0,0,width,height)>0u);
        transition.frame=NULL;transition.frame_width=transition.frame_height=0;
        transition.frame_w=transition.frame_h=0.0f;transition.kind=MODERN_TRANSITION_LOADING;
        CHECK(render_transition_immutable(menu,&transition,&canvas));memcpy(before,pixels,bytes);
        CHECK(render_transition_immutable(menu,&transition,&canvas));CHECK(!memcmp(before,pixels,bytes));
        for(int y=0;y<height;++y)for(int x=width;x<stride;++x)CHECK(pixels[y*stride+x]==UINT32_C(0xabcdef01));
        for(unsigned bad=0u;bad<10u;++bad){
            ModernMenuTransition invalid=transition;UiCanvas invalid_canvas=canvas;
            switch(bad){
            case 0u:invalid.duration=0u;break;
            case 1u:invalid.kind=(ModernMenuTransitionKind)4;break;
            case 2u:invalid.kind=(ModernMenuTransitionKind)-1;break;
            case 3u:invalid.frame=race;invalid.frame_width=0;break;
            case 4u:invalid.frame=race;invalid.frame_width=width;invalid.frame_height=0;break;
            case 5u:invalid.frame=race;invalid.frame_width=width;invalid.frame_height=height;invalid.frame_w=0.0f;break;
            case 6u:invalid_canvas.height=0;break;
            case 7u:invalid_canvas.pixels=NULL;break;
            case 8u:invalid_canvas.stride=invalid_canvas.width-1;break;
            default:invalid.frame=race;invalid.frame_width=width;invalid.frame_height=height;
                invalid.frame_w=(float)width;invalid.frame_h=(float)height;invalid.frame_x=NAN;break;
            }
            CHECK(!modern_menu_render_transition(menu,&invalid,&invalid_canvas));CHECK(!memcmp(before,pixels,bytes));
        }
        CHECK(!modern_menu_render_transition(NULL,&transition,&canvas));
        CHECK(!modern_menu_render_transition(menu,NULL,&canvas));CHECK(!modern_menu_render_transition(menu,&transition,NULL));
        CHECK(!memcmp(pack_before,&sparse,sizeof sparse) && !memcmp(section_before,&section,sizeof section));
        CHECK(!memcmp(race,saved,count*sizeof *race));
        modern_menu_destroy(menu);free(before);free(pixels);free(saved);free(race);
    }
}

static void modern_mods_pixels(void)
{
    const char ids[2][PORTABLE_MOD_ID_CAPACITY] = {
        "missing-custom-model-with-a-long-readable-identity", "missing-second-mod"};
    PortableModManager *manager = portable_mod_manager_create("missing-modern-menu-test-directory", ids, 2u);
    CHECK(manager != NULL && portable_mod_manager_count(manager) == 2u);
    char notice[256];
    memset(notice, 'x', sizeof notice);
    memcpy(notice, "Dependency rejected: ", 21u);
    memcpy(notice + sizeof notice - 29u, " required dependency missing", 28u);
    notice[sizeof notice - 1u] = '\0';
    PortableFrontendFlow flow = {.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU,.tick=120u};
    ModernMenuMods mods = {.manager=manager,.show_order=1u,.notice=notice};
    ModernMenuOptions options = {.page=MODERN_MENU_OPTIONS_MODS,.title="Mods",.mods=&mods};
    const PortableAssetPackView pack = {0};
    ModernMenuView view = {.pack=&pack,.flow=&flow,.options=&options};
    for (unsigned wide=0u;wide<2u;++wide) {
        const int width=wide?1280:240,height=wide?720:160,stride=width+7;
        const size_t count=(size_t)stride*height;
        uint32_t *pixels=malloc(count*sizeof *pixels),*first=malloc(count*sizeof *first);
        CHECK(pixels && first);
        for(size_t i=0u;i<count;++i)pixels[i]=UINT32_C(0xabcdef01);
        UiCanvas canvas;ui_canvas_init(&canvas,pixels,width,height,stride);
        ModernMenu *menu=modern_menu_create();CHECK(menu!=NULL);
        ModernMenuMods saved_mods;memcpy(&saved_mods,&mods,sizeof mods);
        ModernMenuOptions saved_options;memcpy(&saved_options,&options,sizeof options);
        PortableFrontendFlow saved_flow;memcpy(&saved_flow,&flow,sizeof flow);
        CHECK(modern_menu_render(menu,&view,&canvas));
        memcpy(first,pixels,count*sizeof *first);
        CHECK(modern_menu_render(menu,&view,&canvas));
        CHECK(!memcmp(first,pixels,count*sizeof *first));
        CHECK(!memcmp(&saved_mods,&mods,sizeof mods) &&
            !memcmp(&saved_options,&options,sizeof options) && !memcmp(&saved_flow,&flow,sizeof flow));
        mods.notice_error=1u;
        CHECK(modern_menu_render(menu,&view,&canvas));
        CHECK(memcmp(first,pixels,count*sizeof *first)!=0);
        mods.notice_error=0u;
        for(unsigned reviewing=1u;reviewing<=3u;++reviewing) {
            mods.reviewing=reviewing;
            CHECK(modern_menu_render(menu,&view,&canvas));
            CHECK(memcmp(first,pixels,count*sizeof *first)!=0);
        }
        mods.reviewing=0u;
        mods.action_open=1u;
        for(unsigned action=0u;action<5u;++action) {
            mods.action_cursor=action;
            CHECK(modern_menu_render(menu,&view,&canvas));
            CHECK(memcmp(first,pixels,count*sizeof *first)!=0);
        }
        mods.action_open=0u;mods.action_cursor=0u;
        const unsigned pages=modern_menu_mod_detail_pages(manager,0u,1,0,notice);
        CHECK(pages>1u);
        mods.detail_page=pages-1u;
        CHECK(modern_menu_render(menu,&view,&canvas));
        CHECK(memcmp(first,pixels,count*sizeof *first)!=0);
        mods.detail_page=0u;mods.show_order=0u;mods.manager=NULL;
        CHECK(modern_menu_render(menu,&view,&canvas));
        CHECK(memcmp(first,pixels,count*sizeof *first)!=0);
        mods=saved_mods;
        CHECK(portable_mod_manager_count(manager)==2u &&
            strcmp(portable_mod_manager_id(manager,0u),ids[0])==0 &&
            strcmp(portable_mod_manager_id(manager,1u),ids[1])==0);
        for(int y=0;y<height;++y)for(int x=width;x<stride;++x)
            CHECK(pixels[(size_t)y*stride+x]==UINT32_C(0xabcdef01));
        modern_menu_destroy(menu);free(first);free(pixels);
    }
    portable_mod_manager_free(manager);
}

/* The loading card draws at any size from embedded assets only; the live
 * layer stays inside its rectangle and shows each reported step. */
static void modern_loading_pixels(void)
{
    static const int sizes[][2] = {{240, 160}, {640, 360}, {1280, 720}, {1080, 1920}, {2560, 1080}};
    static const char *const phases[] = {"Checking mod content and order", "Preparing courses, music and menus"};
    for (size_t size = 0u; size < sizeof sizes / sizeof sizes[0]; ++size)
    {
        const int width = sizes[size][0], height = sizes[size][1];
        const size_t count = (size_t)width * (size_t)height;
        uint32_t *still = malloc(count * sizeof *still), *live = malloc(count * sizeof *live),
                 *other = malloc(count * sizeof *other);
        CHECK(still && live && other);
        ModernMenu *menu = modern_menu_create();
        CHECK(menu != NULL);
        ModernMenuLoading loading = {.title = "Turning on Retro Pack", .detail = "2 mods: Base, Retro Pack",
                                     .phases = phases, .phase_count = 2u, .cancellable = 1u,
                                     .tick = 90u};
        UiCanvas canvas;
        ui_canvas_init(&canvas, still, width, height, width);
        CHECK(modern_menu_render_loading(menu, &loading, &canvas, 0));
        int rect[4];
        modern_menu_loading_live_rect(width, height, rect);
        CHECK(rect[0] >= 0 && rect[1] >= 0 && rect[2] <= width && rect[3] <= height);
        CHECK(rect[2] > rect[0] && rect[3] > rect[1]);
        memcpy(live, still, count * sizeof *live);
        ui_canvas_init(&canvas, live, width, height, width);
        CHECK(modern_menu_render_loading(menu, &loading, &canvas, 1));
        size_t changed = 0u;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const size_t i = (size_t)y * (size_t)width + (size_t)x;
                const int inside = x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3];
                CHECK(inside || live[i] == still[i]);
                changed += live[i] != still[i];
            }
        CHECK(changed > count / 200u);
        loading.phase = 1u;
        memcpy(other, still, count * sizeof *other);
        ui_canvas_init(&canvas, other, width, height, width);
        CHECK(modern_menu_render_loading(menu, &loading, &canvas, 1));
        CHECK(memcmp(other, live, count * sizeof *other) != 0);
        /* A step that cannot stop shows no cancel hint. */
        loading.cancellable = 0u;
        ui_canvas_init(&canvas, other, width, height, width);
        CHECK(modern_menu_render_loading(menu, &loading, &canvas, 0));
        CHECK(memcmp(other, still, count * sizeof *other) != 0);
        loading.cancellable = 1u;
        loading.cancelling = 1;
        ui_canvas_init(&canvas, other, width, height, width);
        CHECK(modern_menu_render_loading(menu, &loading, &canvas, 0));
        CHECK(memcmp(other, still, count * sizeof *other) != 0);
        loading.phases = NULL;
        CHECK(!modern_menu_render_loading(menu, &loading, &canvas, 1));
        CHECK(!modern_menu_render_loading(NULL, &loading, &canvas, 1));
        modern_menu_destroy(menu);
        free(still);
        free(live);
        free(other);
    }
}

static void external_modal_backgrounds(void)
{
    enum
    {
        W = 240,
        H = 160,
        STRIDE = 245
    };
    uint32_t *pixels = malloc((size_t)STRIDE * H * sizeof *pixels);
    uint32_t *copy = malloc((size_t)STRIDE * H * sizeof *copy);
    CHECK(pixels && copy);
    ModernMenu *menu = modern_menu_create();
    CHECK(menu);
    for (size_t i = 0; i < (size_t)STRIDE * H; ++i)
        pixels[i] = UINT32_C(0x12345678);
    UiCanvas canvas;
    ui_canvas_init(&canvas, pixels, W, H, STRIDE);
    ModernMenuPause pause = {
        .item_count = 1, .labels = {"Continue"}, .tick = 64, .external_background = 1};
    const ModernMenuPause pause_before = pause;
    CHECK(modern_menu_render_pause(menu, &pause, &canvas));
    CHECK(canvas.premultiplied_alpha && !memcmp(&pause, &pause_before, sizeof pause));
    size_t partial = 0, opaque = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < STRIDE; ++x)
        {
            const uint32_t color = pixels[y * STRIDE + x];
            if (x >= W)
            {
                CHECK(color == UINT32_C(0x12345678));
                continue;
            }
            const unsigned alpha = color >> 24u;
            CHECK(((color >> 16u) & 255u) <= alpha && ((color >> 8u) & 255u) <= alpha &&
                  (color & 255u) <= alpha);
            partial += alpha > 0u && alpha < 255u;
            opaque += alpha == 255u;
        }
    CHECK(partial > 1000u && opaque > 0u);
    memcpy(copy, pixels, (size_t)STRIDE * H * sizeof *copy);
    pause.external_background = 0;
    CHECK(!modern_menu_render_pause(menu, &pause, &canvas));
    CHECK(!memcmp(copy, pixels, (size_t)STRIDE * H * sizeof *copy));
    ModernMenuResults results = {.external_background = 1, .title = "Results", .tick = 0};
    CHECK(render_results_immutable(menu, &results, &canvas));
    CHECK(canvas.premultiplied_alpha);
    /* At zero animation the right-hand race area has only the fixed dim.
     * Its premultiplied bytes must composite over any opaque GPU picture. */
    const uint32_t shade = pixels[(H / 2) * STRIDE + W - 1];
    CHECK((shade >> 24u) == 112u);
    CHECK(((shade >> 16u) & 255u) == (2u * 112u + 127u) / 255u);
    CHECK(((shade >> 8u) & 255u) == (18u * 112u + 127u) / 255u);
    CHECK((shade & 255u) == (48u * 112u + 127u) / 255u);
    for (unsigned channel = 0; channel < 3u; ++channel)
    {
        const unsigned contribution = (shade >> (channel * 8u)) & 255u;
        const unsigned composite = contribution + (192u * (255u - (shade >> 24u)) + 127u) / 255u;
        CHECK(composite > contribution && composite < 192u);
    }
    results.tick = 64;
    results.item_count = 1;
    results.labels[0] = "Continue";
    CHECK(render_results_immutable(menu, &results, &canvas));
    opaque = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const uint32_t color = pixels[y * STRIDE + x];
            const unsigned alpha = color >> 24u;
            CHECK(((color >> 16u) & 255u) <= alpha && ((color >> 8u) & 255u) <= alpha &&
                  (color & 255u) <= alpha);
            opaque += alpha == 255u;
        }
    CHECK(opaque > 0u);
    memcpy(copy, pixels, (size_t)STRIDE * H * sizeof *copy);
    results.external_background = 0;
    CHECK(!modern_menu_render_results(menu, &results, &canvas));
    CHECK(!memcmp(copy, pixels, (size_t)STRIDE * H * sizeof *copy));
    modern_menu_destroy(menu);
    free(copy);
    free(pixels);
}

static void modern_games_pixels(void)
{
    PortableFrontendFlow flow={.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU,.tick=120};
    ModernMenuGames games={0};
    ModernMenuOptions options={.page=MODERN_MENU_OPTIONS_GAMES,.title="Imported games",.games=&games};
    const PortableAssetPackView pack={0};
    ModernMenuView view={.pack=&pack,.flow=&flow,.options=&options};
    const int widths[]={640,1280},heights[]={480,720};
    for(unsigned shape=0;shape<2;++shape){
        const int width=widths[shape],height=heights[shape],stride=width+7;
        uint32_t *pixels=malloc((size_t)stride*height*sizeof *pixels);CHECK(pixels);
        for(int i=0;i<stride*height;++i)pixels[i]=0xabcdef01u;
        UiCanvas canvas;ui_canvas_init(&canvas,pixels,width,height,stride);
        ModernMenu *menu=modern_menu_create();CHECK(menu);
        /* Every row at 4:3; the wide shape checks extents once (debug pixel
         * loops are slow on a loaded host). */
        for(unsigned row=0;row<(shape?1u:5u);++row){
            games.row=row;games.game=row%3;view.gamepad=(uint8_t)(row&1);
            const ModernMenuGames saved=games;
            CHECK(modern_menu_render(menu,&view,&canvas));
            CHECK(!memcmp(&saved,&games,sizeof games));
            for(int y=0;y<height;++y)for(int x=width;x<stride;++x)CHECK(pixels[y*stride+x]==0xabcdef01u);
        }
        modern_menu_destroy(menu);free(pixels);
    }
}

int main(void)
{
    recorded_presentation();
    owned_draw_commands();
    recorded_large_source_mutation();
    external_modal_backgrounds();
    modern_mods_pixels();
    modern_loading_pixels();
    modern_games_pixels();
    modern_results_pixels();
    modern_transition_pixels();
    /* The embedded table names every stock driver, course and cup. */
    static const uint32_t drivers[] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    for (size_t i = 0; i < sizeof drivers / sizeof drivers[0]; ++i) {
        unsigned speed = 0u, weight = 0u;
        CHECK(portable_menu_driver_name(drivers[i]) != NULL);
        CHECK(portable_menu_driver_stats(drivers[i], &speed, &weight));
        CHECK(speed >= 1u && speed <= 5u && weight >= 1u && weight <= 5u);
    }
    CHECK(strcmp(portable_menu_driver_name(1u), "Mario") == 0);
    for (uint32_t course = 24u; course <= 43u; ++course) CHECK(portable_menu_course_name(course) != NULL);
    for (uint32_t course = 64u; course <= 83u; ++course) CHECK(portable_menu_course_name(course) != NULL);
    for (uint32_t course = 96u; course <= 99u; ++course) CHECK(portable_menu_course_name(course) != NULL);
    for (unsigned cup = 0u; cup <= 10u; ++cup) CHECK(portable_menu_cup_name(cup) != NULL);
    CHECK(portable_menu_driver_name(9999u) == NULL);
    /* Invalid documents are rejected whole and keep the loaded table. */
    static const char bad_version[] = "{\"version\":2,\"drivers\":[]}";
    static const char unknown_key[] = "{\"version\":1,\"karts\":[]}";
    static const char bad_stat[] = "{\"version\":1,\"drivers\":[{\"id\":1,\"name\":\"A\",\"speed\":9}]}";
    CHECK(!portable_menu_names_parse(bad_version, sizeof bad_version - 1u));
    CHECK(!portable_menu_names_parse(unknown_key, sizeof unknown_key - 1u));
    CHECK(!portable_menu_names_parse(bad_stat, sizeof bad_stat - 1u));
    CHECK(strcmp(portable_menu_driver_name(1u), "Mario") == 0);
    static const char custom[] = "{\"version\":1,\"drivers\":[{\"id\":200,\"name\":\"Rider\",\"speed\":2,\"weight\":4}],"
                                 "\"cups\":[{\"cup\":0,\"name\":\"First\"}]}";
    CHECK(portable_menu_names_parse(custom, sizeof custom - 1u));
    CHECK(strcmp(portable_menu_driver_name(200u), "Rider") == 0 && portable_menu_driver_name(1u) == NULL);
    CHECK(strcmp(portable_menu_cup_name(0u), "First") == 0);

    /* Shapes and text draw inside the canvas and respect the clip. */
    enum { W = 160, H = 90 };
    static uint32_t pixels[W * H];
    UiCanvas canvas;
    ui_canvas_init(&canvas, pixels, W, H, W);
    ui_canvas_clear(&canvas, 0xff000000u);
    UiPaint white = ui_paint_solid(0xffffffffu);
    ui_fill_round_rect(&canvas, 10.0f, 10.0f, 40.0f, 20.0f, 6.0f, 0.0f, &white, 1.0f, 1.0f);
    CHECK(pixels[20 * W + 30] == 0xffffffffu && pixels[5 * W + 5] == 0xff000000u);
    ui_canvas_clip(&canvas, 0, 0, 80, H);
    ui_fill_rect(&canvas, 0.0f, 50.0f, (float)W, 10.0f, 0xffff0000u, 1.0f);
    CHECK(pixels[55 * W + 40] == 0xffff0000u && pixels[55 * W + 120] == 0xff000000u);
    ui_canvas_unclip(&canvas);
    CHECK(ui_font_ready());
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, 20.0f, 0xffffffffu);
    CHECK(ui_text_width(&style, "Mario Kart") > 50.0f);
    ui_canvas_clear(&canvas, 0xff000000u);
    ui_text_draw_centered(&canvas, &style, 80.0f, 45.0f, UI_ALIGN_CENTER, "Mario Kart", 1.0f);
    unsigned lit = 0u;
    for (size_t i = 0; i < (size_t)W * H; ++i) lit += (pixels[i] & 0xffu) > 0x80u;
    CHECK(lit > 100u);
    ui_font_release();
    puts("modern menu: PASS");
    return 0;
}
