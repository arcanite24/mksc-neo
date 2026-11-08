#include "modern_menu_internal.h"
#include "game_library.h"
#include "character_menu_renderer.h"
#include "course_3d.h"
#include <stdio.h>
#include <string.h>

static void text(MmCtx *x,const char *label,float px,float py,float width,float size,uint32_t color)
{
    UiTextStyle style=ui_text_style(UI_FONT_BLACK,mm_s(x,size),color);
    style=ui_text_fit(&style,label,mm_s(x,width));
    ui_text_draw_centered(x->c,&style,mm_x(x,px),mm_y(x,py),UI_ALIGN_LEFT,label,1);
}
static void game_art(MmCtx *x,const ModernMenuGames *games,size_t index,float left)
{
    const PortableLibraryGame *game=portable_game_library_at(index);
    if(game->importer==PORTABLE_GAME_BASE){
        const UiImage *art=mm_art(x,ui_art_animation_frame(x->view->pack,PORTABLE_CHARACTER_PREVIEW,x->tick));
        if(art)ui_draw_image(x->c,art,mm_x(x,left+302),mm_y(x,340),mm_s(x,140),mm_s(x,140),1,UI_IMAGE_NEAREST,MM_WHITE,0);
    }else if(index<MODERN_MENU_GAMES_MAX&&games->art[index].pixels){
        ui_draw_image(x->c,&games->art[index],mm_x(x,left+250),mm_y(x,350),mm_s(x,232),mm_s(x,130),1,
            UI_IMAGE_NEAREST,MM_WHITE,mm_s(x,8));
    }else if(index<MODERN_MENU_GAMES_MAX&&games->art_loading[index])
        text(x,"Loading preview...",left+250,410,232,24,0xfff3e8ffu);
}
void mm_games_overlay(MmCtx *x,const ModernMenuGames *games)
{
    if(!games)return;
    for(size_t i=0;i<portable_game_library_count();++i){
        const PortableLibraryGame *game=portable_game_library_at(i);
        const PortableModCatalogEntry *entry=portable_game_library_entry(games->manager,i);
        const int imported=game->importer==PORTABLE_GAME_BASE||(entry&&entry->valid);
        const int focused=games->row==0 && games->game==i;
        const float left=150.0f+(float)i*548.0f;
        UiPaint cover=ui_paint_linear(mm_x(x,left),mm_y(x,270),imported?game->color:0xff484c52u,
            mm_x(x,left+360),mm_y(x,586),imported?0xff101c3au:0xff1f2227u);
        ui_fill_round_rect(x->c,mm_x(x,left),mm_y(x,270),mm_s(x,516),mm_s(x,316),mm_s(x,12),0,&cover,1.2f,1);
        text(x,game->system,left+26,310,450,24,imported?0xfff3e8ffu:0xffdededeu);
        if(imported)game_art(x,games,i,left);
        text(x,game->name,left+26,490,464,45,MM_WHITE);
        const char *status=game->importer==PORTABLE_GAME_BASE?"Base game / Ready to race":
            entry&&entry->valid?(portable_mod_manager_index(games->manager,game->id)>=0?"Imported / Selected":"Imported / Not selected"):
            entry?"Import needs repair":"Not imported";
        text(x,status,left+26,540,464,26,0xffe0e3e9u);
        if(focused)mm_corner_brackets(x,mm_x(x,left-5),mm_y(x,265),mm_s(x,526),mm_s(x,326),MM_YELLOW,1);
    }
    const PortableLibraryGame *selected=portable_game_library_at(games->game);
    const PortableModCatalogEntry *entry=portable_game_library_entry(games->manager,games->game);
    const int installed=selected && (selected->importer==PORTABLE_GAME_BASE||(entry&&entry->valid));
    const char *action=!selected?"":selected->importer==PORTABLE_GAME_BASE?"Base game is always available":
        installed?"Enable or disable this game, or import it again.":"Confirm: Import from your ROM";
    text(x,action,176,628,1550,28,MM_WHITE);
    static const char *const labels[]={"Karts & drivers","Items","Course models"};
    for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind){
        const char *source=portable_mod_manager_source(games->manager,kind);
        const char *name=!source[0]?"Mod order":!strcmp(source,"@base")?"Super Circuit":source;
        const PortableModCatalog *catalog=portable_mod_manager_catalog(games->manager);
        for(size_t j=0;catalog&&j<catalog->count;++j)
            if(!strcmp(source,catalog->entries[j].id))name=catalog->entries[j].manifest.name;
        const int focused=games->row==kind+1;
        mm_tab(x,160,670+(float)kind*78,1580,68,labels[kind],NULL,focused,1,MM_ICON_NONE,0,0);
        char value[56];
        snprintf(value,sizeof value,"%s%.44s%s%s",focused?"<  ":"",name,strlen(name)>44?"...":"",focused?"  >":"");
        text(x,value,900,704+(float)kind*78,780,30,focused?0xff3a2a00u:MM_INK);
    }
    mm_tab(x,160,910,560,64,"Apply asset choices",NULL,games->row==4,1,MM_ICON_FLAG,0,0);
    UiTextStyle notice=ui_text_style(UI_FONT_BOLD,mm_s(x,24),games->notice_error?0xffffb8a8u:MM_WHITE);
    ui_text_draw_wrapped(x->c,&notice,mm_x(x,760),mm_y(x,910),mm_s(x,980),mm_s(x,25),3,UI_ALIGN_LEFT,
        games->notice&&games->notice[0]?games->notice:"Missing matching assets keep the current source.",1);
    const MmHint hints[]={{MM_BUTTON_B,"Back"},{MM_BUTTON_LR,"Category"},{MM_BUTTON_DPAD,"Choose"},
        {MM_BUTTON_A,"Select"},{MM_BUTTON_SELECT,"Import"}};
    mm_footer(x,NULL,hints,games->row==0&&selected&&selected->importer!=PORTABLE_GAME_BASE?5:4);
}
