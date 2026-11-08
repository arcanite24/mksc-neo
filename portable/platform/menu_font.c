#include "menu_font.h"
#include "native_image.h"
#include "ui_text.h"
#include "frontend_image_renderer.h"
#include <string.h>

static int open_image(const PortableAssetPackView *pack,uint32_t id,PortableNativeImage *image)
{
    PortableAssetSectionView section;
    return pack!=NULL&&portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS,
        id,&section)==PORTABLE_ASSET_PACK_OK&&portable_native_image_open(section.bytes,section.size,image);
}

/* Copies one opaque source rectangle; transparent pixels keep the frame. */
static void blit(const PortableNativeImage *image,PortableSoftwareFrame *frame,
    unsigned source_x,unsigned source_y,unsigned width,unsigned height,int x,int y)
{
    for(unsigned dy=0u;dy<height;++dy)for(unsigned dx=0u;dx<width;++dx){
        const int tx=x+(int)dx,ty=y+(int)dy;
        if(tx<0||ty<0||(size_t)tx>=frame->width||(size_t)ty>=frame->height)continue;
        if(source_x+dx>=image->width||source_y+dy>=image->height)continue;
        const uint32_t pixel=portable_native_image_pixel(image,source_x+dx,source_y+dy);
        if((pixel>>24)==255u)portable_software_frame_store(frame,(size_t)ty*frame->width+(size_t)tx,pixel);
    }
}

static int text_span(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,const char *text,size_t length)
{
    PortableNativeImage font;
    if(!open_image(pack,PORTABLE_MENU_FONT,&font))return x;
    for(const unsigned char *c=(const unsigned char *)text;length-->0u&&*c!='\0';++c,x+=PORTABLE_MENU_FONT_WIDTH){
        unsigned glyph=*c>=0x20u&&*c<0x80u?*c-0x20u:'?'-0x20u;
        /* The ROM font stores the apostrophe before the ampersand. */
        if(*c=='&'||*c=='\'')glyph^=1u;
        if(glyph!=0u)blit(&font,frame,glyph*PORTABLE_MENU_FONT_WIDTH,0u,
            PORTABLE_MENU_FONT_WIDTH,PORTABLE_MENU_FONT_HEIGHT,x,y);
    }
    return x;
}

int portable_menu_text(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,const char *text)
{
    if(frame==NULL||text==NULL)return x;
    text=portable_ui_text_resolve(frame->ui_text,text);
    return text_span(pack,frame,x,y,text,strlen(text));
}

unsigned portable_menu_text_wrapped(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,unsigned columns,int line_height,unsigned max_lines,const char *text)
{
    unsigned lines=0u;
    if(frame==NULL||text==NULL||columns==0u)return 0u;
    text=portable_ui_text_resolve(frame->ui_text,text);
    while(*text!='\0'&&lines<max_lines){
        while(*text==' ')++text;
        size_t length=strlen(text);
        if(length==0u)break;
        if(length>columns){
            length=columns;
            while(length>0u&&text[length]!=' ')--length;
            if(length==0u)length=columns;
        }
        text_span(pack,frame,x,y+(int)lines*line_height,text,length);
        text+=length;++lines;
    }
    return lines;
}

static uint32_t dim_pixel(uint32_t pixel)
{
    uint32_t value=pixel&UINT32_C(0xff000000);
    for(unsigned shift=0u;shift<24u;shift+=8u){
        const uint32_t channel=(((pixel>>shift)&255u)>>3)*3u>>2;
        value|=(channel<<3|channel>>2)<<shift;
    }
    return value;
}

void portable_menu_dim(PortableSoftwareFrame *frame)
{
    if(frame==NULL)return;
    const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
    for(size_t y=0u;y<frame->height*scale;++y)for(size_t x=0u;x<frame->width*scale;++x)
        portable_software_frame_store_physical(frame,x,y,dim_pixel(frame->pixels[y*stride+x]));
}

int portable_menu_text_centered(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,const char *text)
{
    if(frame==NULL||text==NULL)return x;
    text=portable_ui_text_resolve(frame->ui_text,text);
    const int width=(int)strlen(text)*PORTABLE_MENU_FONT_WIDTH;
    return text_span(pack,frame,x-width/2,y,text,strlen(text));
}

PortableFrontendPresentationStatus portable_menu_frame(const PortableAssetPackView *pack,
    PortableSoftwareFrame *frame,int x,int y,unsigned width,unsigned height)
{
    PortableNativeImage border;
    if(frame==NULL||width<16u||height<16u)return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    if(!open_image(pack,PORTABLE_MENU_FRAME,&border))return PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    const unsigned columns=width/8u,rows=height/8u;
    for(unsigned row=0u;row<rows;++row)for(unsigned column=0u;column<columns;++column){
        const unsigned sx=column==0u?0u:column+1u==columns?2u:1u;
        const unsigned sy=row==0u?0u:row+1u==rows?2u:1u;
        if(sx==1u&&sy==1u)continue;
        blit(&border,frame,sx*8u,sy*8u,8u,8u,x+(int)column*8,y+(int)row*8);
    }
    return PORTABLE_FRONTEND_PRESENTATION_OK;
}

PortableFrontendPresentationStatus portable_menu_panel(const PortableAssetPackView *pack,
    PortableSoftwareFrame *frame,int x,int y,unsigned width,unsigned height)
{
    if(frame==NULL||width<16u||height<16u)return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    const int right=x+(int)(width/8u*8u),bottom=y+(int)(height/8u*8u);
    PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,
        PORTABLE_MENU_BACKGROUND,frame,0,0,x,y,right,bottom);
    if(status!=PORTABLE_FRONTEND_PRESENTATION_OK)return status;
    const int scale=(int)portable_software_frame_scale(frame);
    const size_t stride=portable_software_frame_stride(frame);
    for(int row=(y<0?0:y)*scale;row<bottom*scale&&(size_t)row<frame->height*(size_t)scale;++row)
        for(int column=(x<0?0:x)*scale;column<right*scale&&(size_t)column<frame->width*(size_t)scale;++column)
            portable_software_frame_store_physical(frame,(size_t)column,(size_t)row,
                dim_pixel(frame->pixels[(size_t)row*stride+(size_t)column]));
    return portable_menu_frame(pack,frame,x,y,width,height);
}

int portable_menu_notice(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,const char *text)
{
    if(frame==NULL||text==NULL)return 0;
    const unsigned lines=portable_menu_text_wrapped(NULL,frame,0,0,26u,16,3u,text);
    const PortableFrontendPresentationStatus panel=portable_menu_panel(pack,frame,8,24,224u,lines*16u+16u);
    if(panel!=PORTABLE_FRONTEND_PRESENTATION_OK&&panel!=PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)return 0;
    if(lines==1u)(void)portable_menu_text_centered(pack,frame,120,32,text);
    else (void)portable_menu_text_wrapped(pack,frame,16,32,26u,16,3u,text);
    return 1;
}
