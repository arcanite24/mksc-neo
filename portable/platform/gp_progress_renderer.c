#include "gp_progress_renderer.h"
#include "course_menu_renderer.h"
#include "frontend_image_renderer.h"

/* Packs without the record art (test fixtures) leave the page bare. */
static int draw(const PortableAssetPackView *pack,uint32_t id,int x,int y,PortableSoftwareFrame *frame)
{
    const PortableFrontendPresentationStatus status=portable_frontend_image_draw(pack,id,frame,x,y,0,0,240,160);
    return status==PORTABLE_FRONTEND_PRESENTATION_OK||status==PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
}

int portable_gp_progress_render(const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableGpProgress *progress,PortableSoftwareFrame *frame)
{
    if(pack==NULL||flow==NULL||progress==NULL||frame==NULL||frame->pixels==NULL||
       frame->width!=240u||frame->height!=160u||flow->page!=PORTABLE_FRONTEND_PAGE_COURSE||
       flow->mode_cursor!=0u||flow->course_phase!=0u||flow->race_speed>=3u)
        return 0;
    /* Custom cups keep no medals or ratings. */
    if(portable_track_set_of_cup(flow->course_cup)==PORTABLE_TRACK_SET_CUSTOM)return 1;
    if(flow->course_cup>=PORTABLE_FRONTEND_ARENA_CUP)return 0;
    const unsigned set=flow->course_cup/5u,cup=flow->course_cup%5u,speed=flow->race_speed;
    const int grade=progress->grade[set][cup][speed];
    const unsigned rating=progress->rating[set][cup][speed];
    if(grade<-1||grade>2||(rating>7u&&rating!=255u))return 0;
    /* sub_8009590 draws the RANK cell at (100,64) and sub_8009754 the
     * spinning trophy at (112,88), a new frame every 7 ticks; an unraced
     * cup/class shows neither. */
    if(rating<=7u&&!draw(pack,PORTABLE_COURSE_RANK+rating,100,64,frame))return 0;
    return grade<0||draw(pack,PORTABLE_COURSE_TROPHY+(unsigned)grade*16u+(flow->tick/7u)%16u,112,88,frame);
}
