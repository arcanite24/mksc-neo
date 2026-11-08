#include "course_contacts.h"

#include <stddef.h>
#include "diagnostic.h"
#include "driver_palette.h"
#include "spatial_audio.h"

typedef struct {
    PortableCourseContacts *world;
    int ok;
} CourseContact;

static void plain_contact_audio(PortableDriverPilotState *driver,uint16_t song)
{
    if(driver->steering.state_flags&0x20u)return;
    driver->contact_audio=(PortableDriverContactAudio){.serial=driver->contact_audio.serial+1u,
        .song_id=song,.volume=256u,.restart=1u};
}

static int impact_voice(PortableCourseContacts *world)
{
    if(world->listener_flags&UINT32_C(0x20))return 1;
    PortableCourseImpactSounds *sounds=world->impact_sounds;
    if(sounds->count>=PORTABLE_COURSE_IMPACT_CAPACITY)return 0;
    uint16_t volume=256u;
    if(!world->is_player){
        volume=portable_spatial_audio_volume((uint32_t)world->driver->pose.position_x,
            (uint32_t)world->driver->pose.position_y,
            (uint32_t)world->listener_x,(uint32_t)world->listener_y);
        if(volume<64u)volume=64u;
    }
    sounds->events[sounds->count++]=(PortableCourseImpactSound){.volume=volume,
        .character=world->driver_class,.rival=(uint8_t)!world->is_player,
        .variant=(world->driver->item_cue_flags_58&0x10u)?0u:1u};
    return 1;
}

static void course_driver_contact(PortableCollisionActor *kart,
    PortableCollisionActor *other, void *user)
{
    CourseContact *contact=user;
    PortableCourseContacts *world=contact->world;
    PortableDriverPilotState *driver=world->driver;
    if((other->source.type_flags&UINT16_C(0xf000))==UINT16_C(0x2000))return;
    ++world->events.contacts;
    const uint16_t family=world->is_player?UINT16_C(0x2001):UINT16_C(0x2002);
    if(other->source.type_flags==0x1007u){
        /* 0128 returns before +5A/+38 and damage dispatch for candle Boos. */
        if(driver->contact_guard_66==0u && driver->hop.mode<=1u &&
           world->is_player && driver->invulnerability_timer_6a==0u && !(driver->steering.state_flags&0x20u))
            driver->contact_audio=(PortableDriverContactAudio){
                .serial=driver->contact_audio.serial+1u,.song_id=380u,.volume=256u,.pitch=0,.restart=0u};
        portable_driver_palette_boo_contact(family,&driver->palette_requests_194);
        return;
    }
    const int kamek=other->source.type_flags==UINT16_C(0x101b);
    const int pier_boo=other->source.type_flags==UINT16_C(0x1008);
    const int teepee=other->source.type_flags==UINT16_C(0x1018);
    const int mechakoopa=other->source.type_flags==UINT16_C(0x101c);
    const int prop=other->source.type_flags==UINT16_C(0x1000);
    const int thwomp=other->source.type_flags==UINT16_C(0x1002);
    const int seagull=other->source.type_flags==UINT16_C(0x100c);
    const int cannonball=other->source.type_flags==UINT16_C(0x1013);
    const int penguin=other->source.type_flags==UINT16_C(0x1016);
    const int snowman=other->source.type_flags==UINT16_C(0x1012);
    const int piranha=other->source.type_flags==UINT16_C(0x1009);
    const int star=other->source.type_flags==UINT16_C(0x1011);
    const int cloud=other->source.type_flags==UINT16_C(0x1010);
    const int mouse=other->source.type_flags==UINT16_C(0x1015);
    const int crab=other->source.type_flags==UINT16_C(0x1003);
    const int lava=other->source.type_flags==UINT16_C(0x1004);
    /* 59938 rocks share 0128's lava entries but have empty own callbacks. */
    const int rock=other->source.type_flags==UINT16_C(0x101d);
    if(seagull){
        if(world->is_player && driver->contact_guard_66==0u && driver->hop.mode<=1u && driver->normal_collision_timer_38<=1u){
            if(driver->invulnerability_timer_6a)plain_contact_audio(driver,160u);
            else portable_driver_pilot_request_contact_audio(driver,385u,1u);
        }
        portable_seagull_contact(&world->seagulls->actors[other->tag],family,
            (int16_t)driver->contact_guard_66,(int16_t)driver->invulnerability_timer_6a);
        return;
    }
    const int16_t guard=(int16_t)driver->contact_guard_66;
    const int16_t protection=(int16_t)driver->invulnerability_timer_6a;
    /* 0128 accepts modes0/1; the protected-object callbacks independently
     * accept any driver mode. The list already applies the +38 filter. */
    if(guard==0 && driver->hop.mode<=1u){
        if(protection!=0){
            if(world->is_player)plain_contact_audio(driver,160u);
        }
        else{
            const int escaping=driver->normal_collision_timer_38!=0u;
            if(!escaping){
                if(world->is_player && !seagull && !piranha && !kamek)portable_driver_pilot_request_contact_audio(driver,(thwomp || prop || mechakoopa)?169u:pier_boo?380u:teepee?371u:(star || crab || mouse || snowman || penguin)?373u:cannonball?170u:cloud?374u:377u,1u);
            }
            if(escaping || cannonball){
                /* FEB8 and timer-one escape both use the FBFC away vector. */
                const int32_t dx=portable_lava_bubble_signed(
                    (uint32_t)driver->pose.position_x-(uint32_t)other->pos_x)/65536;
                const int32_t dy=portable_lava_bubble_signed(
                    (uint32_t)driver->pose.position_y-(uint32_t)other->pos_y)/65536;
                PortableDriverObjectContactResult push;
                const PortableDriverPilotDependencies *deps=world->dependencies;
                if(portable_driver_object_contact_escape((int16_t)dx,(int16_t)dy,0,0,
                        deps->sine_table,deps->sine_count,&push)!=PORTABLE_DRIVER_VECTOR_EFFECT_OK)
                {contact->ok=0;return;}
                driver->motion.component_x=push.resultant_x;
                driver->motion.component_y=push.resultant_y;
                driver->pose.component_x=push.resultant_x;
                driver->pose.component_y=push.resultant_y;
            }
            if((star || snowman || prop) && !escaping){
                const PortableDriverPilotDependencies *deps=world->dependencies;
                PortableDriverObjectContactResult push;
                if(portable_driver_object_contact_resultant(
                        driver->contact_anchor_x_ee,driver->contact_anchor_y_f0,
                        (int16_t)(other->pos_x/65536),(int16_t)(other->pos_y/65536),
                        driver->motion.component_x,driver->motion.component_y,
                        (uint16_t)driver->steering.angle,deps->sine_table,deps->sine_count,
                        &push)!=PORTABLE_DRIVER_VECTOR_EFFECT_OK){contact->ok=0;return;}
                driver->motion.component_x=push.resultant_x;driver->motion.component_y=push.resultant_y;
                driver->pose.component_x=push.resultant_x;driver->pose.component_y=push.resultant_y;
            }
            /* 0128's second table sends lava and rocks to D214(3) after FF80. */
            if(!escaping && (lava || rock)){driver->burn_ticks_72=PORTABLE_DRIVER_BURN_TICKS;++world->events.burns;}
            driver->vector_effect.contact_word_5a=UINT16_C(0x8007);
            driver->steering.state_flags|=UINT32_C(0x1000);
            driver->normal_collision_timer_38=8u;
            if(!escaping){
                if(thwomp){
                    const PortableThwomp *actor=&world->thwomps->actors[other->tag];
                    portable_driver_pilot_thwomp_hit(driver,actor->height_q16,actor->velocity_z_q16);
                    if(driver->flattened_64){
                        /* FD7C stops song164 on every crush; the cue carries it. */
                        if(world->is_player && driver->contact_audio.song_id==169u)
                            driver->contact_audio.stop_song=164u;
                        ++world->events.flattened_hits;world->events.coin_loss+=8u;
                        if(world->events.coin_loss_count==16u){contact->ok=0;return;}
                        world->events.coin_loss_causes[world->events.coin_loss_count++]=4u;
                        if(!impact_voice(world)){contact->ok=0;return;}
                    }
                }else if(star || cloud || cannonball || ((mechakoopa || penguin || mouse || crab || lava || rock) && (driver->steering.state_flags&0x2000u))){
                    /* FEB8/40060, or FF80 with +48 bit2000, enters E490.
                     * TT has no ranked/player coin-loss fields. */
                    driver->forward_acceleration.state_flags |=
                        world->scene_clock & 1u ? UINT16_C(0x0300) : UINT16_C(0x0200);
                    driver->strong_spin_rate_b6=(driver->steering.state_flags & UINT32_C(0x8000)) || world->player_rank_ahead ?
                        UINT16_C(0x1000) : UINT16_C(0x2000);
                    if(cloud)driver->lightning_timer_68=360;
                    driver->recovery_flags_74|=3u;
                    ++world->events.strong_hits;world->events.coin_loss+=4u;
                    if(world->events.coin_loss_count==16u){contact->ok=0;return;}
                    world->events.coin_loss_causes[world->events.coin_loss_count++]=1u;
                    if(!impact_voice(world)){contact->ok=0;return;}
                    /* E490 ends with the local player's spin squeal162. */
                    if(world->is_player && !(driver->steering.state_flags&0x20u)){
                        if(world->piranha_sounds->count==PORTABLE_PIRANHA_CAPACITY){contact->ok=0;return;}
                        world->piranha_sounds->songs[world->piranha_sounds->count++]=162u;
                    }
                }else if(!kamek && !pier_boo && !teepee && !seagull && !piranha && !snowman && !prop)driver->forward_acceleration.state_flags|=UINT16_C(0x1000);
            }
            kart->filter_flags=driver->normal_collision_timer_38;
            driver->steering.response_flags=driver->forward_acceleration.state_flags;
            driver->hop.animation_flags=driver->forward_acceleration.state_flags;
        }
    }
    /* The passive actor callback is separate from 0128. 59E44 checks the
     * driver family/protection, independently of guard66 and driver mode. */
    if(prop){
        if(protection)portable_course_prop_damage(&world->course_props->actors[other->tag]);
    }
    else if(mechakoopa || kamek){
        if(!guard && protection)portable_course_prop_damage(&world->course_props->actors[other->tag]);
    }
    else if(pier_boo){
        /* 59FAC: an unprotected touch marks the ghost unusable and hands the
         * kart to 4886C's haunt; the haunt's owner spawns it after the pass. */
        if(guard)return;
        if(protection)portable_course_prop_damage(&world->course_props->actors[other->tag]);
        else{
            portable_pier_boo_vanish(&world->course_props->actors[other->tag]);
            ++world->events.ghost_invalidations;++world->events.boo_haunts;
        }
    }
    else if(teepee){
        if(guard)return;
        portable_teepee_hit(&world->course_props->actors[other->tag],world->kart_index,
            driver->motion.component_x,driver->motion.component_y);
        ++world->events.ghost_invalidations;
    }
    else if(mouse)portable_mouse_contact(&world->mice->actors[other->tag],family,guard,protection);
    else if(thwomp)
        portable_thwomp_contact(&world->thwomps->actors[other->tag],family,
            guard,protection,driver->motion.component_x,driver->motion.component_y);
    else if(seagull)
        portable_seagull_contact(&world->seagulls->actors[other->tag],family,guard,protection);
    else if(piranha){
        if(portable_piranha_contact(&world->piranhas->actors[other->tag],family,guard,protection,world->is_player) && world->is_player &&
           !(driver->steering.state_flags&0x20u))
        {
            if(world->piranha_sounds->count==PORTABLE_PIRANHA_CAPACITY){contact->ok=0;return;}
            world->piranha_sounds->songs[world->piranha_sounds->count++]=373u;
        }
    }
    else if(penguin)
        portable_penguin_contact(&world->penguins->actors[other->tag],family,guard,protection);
    else if(snowman){
        if(portable_snowman_contact(&world->snowmen->actors[other->tag],family,guard))
            ++world->events.ghost_invalidations;
    }
    else if(crab)
        portable_crab_contact(&world->crabs->actors[other->tag],family,guard,protection);
    else if(!cannonball && !cloud && !star && !rock)
        portable_lava_bubble_contact(&world->lava_bubbles->actors[other->tag],family,
            guard,protection,driver->motion.component_x,driver->motion.component_y);
}

int portable_course_contacts_collide_many(PortableCourseContacts *worlds,size_t driver_count)
{
    if(worlds==NULL || driver_count==0u || driver_count>8u)return 0;
    PortableCourseContacts *world=&worlds[0];
    PortableCollisionList list={0};
    CourseContact contacts[8];
    PortableCollisionActor karts[8];
    for(size_t i=0u;i<driver_count;++i){
        PortableCourseContacts *driver=&worlds[i];
        if(driver->driver==NULL || driver->dependencies==NULL ||
           driver->falling_stars==NULL || driver->thunderclouds==NULL || driver->mice==NULL || driver->course_props==NULL || driver->lava_bubbles==NULL || driver->thwomps==NULL || driver->crabs==NULL ||
           driver->seagulls==NULL || driver->cannonballs==NULL || driver->piranhas==NULL ||
           driver->snowmen==NULL || driver->penguins==NULL || driver->boos==NULL ||
           driver->piranha_sounds==NULL || driver->impact_sounds==NULL || driver->driver_class>=8u || driver->mode_name==NULL ||
           driver->is_player>1u || driver->player_rank_ahead>1u)return 0;
        if(driver->falling_stars!=world->falling_stars || driver->thunderclouds!=world->thunderclouds || driver->mice!=world->mice || driver->course_props!=world->course_props || driver->lava_bubbles!=world->lava_bubbles || driver->thwomps!=world->thwomps ||
           driver->crabs!=world->crabs || driver->seagulls!=world->seagulls ||
           driver->cannonballs!=world->cannonballs || driver->piranhas!=world->piranhas ||
           driver->snowmen!=world->snowmen || driver->penguins!=world->penguins ||
           driver->boos!=world->boos || driver->piranha_sounds!=world->piranha_sounds ||
           driver->impact_sounds!=world->impact_sounds ||
           driver->checkpoint_count!=world->checkpoint_count)return 0;
        driver->events=(PortableCourseContactEvents){0};
        contacts[i]=(CourseContact){.world=driver,.ok=1};
        karts[i]=(PortableCollisionActor){
            .pos_x=driver->driver->pose.position_x,.pos_y=driver->driver->pose.position_y,
            .pos_z=driver->driver->pose.position_z,.size_x=10u,.size_y=10u,.size_z=10u,
            .filter_flags=driver->driver->normal_collision_timer_38>driver->collision_guard?
                driver->driver->normal_collision_timer_38:driver->collision_guard,
            .checkpoint_id=driver->driver->source_route_index_90,
            .source={.type_flags=driver->is_player?UINT16_C(0x2001):UINT16_C(0x2002)},
            .collide=course_driver_contact,.user=&contacts[i]};
        if((driver->driver->recovery_contact_34&1u) &&
           portable_collision_add_active(&list,&karts[i])!=PORTABLE_COLLISION_OK)return 0;
    }
    PortableCollisionActor actors[PORTABLE_COLLISION_MAX_PASSIVE];
    uint8_t order[PORTABLE_COLLISION_MAX_PASSIVE];
    size_t count=0u;
    for(unsigned family=0u;family<13u;++family){
        const size_t actor_count=family==0u?world->lava_bubbles->count:
            family==1u?world->thwomps->count:family==2u?world->crabs->count:family==3u?world->seagulls->count:family==4u?world->cannonballs->count:family==5u?world->piranhas->count:family==6u?world->snowmen->count:family==7u?world->penguins->count:family==8u?world->boos->count:family==9u?world->course_props->count:family==10u?world->mice->count:family==11u?world->thunderclouds->count:world->falling_stars->count;
        const size_t capacity=family==0u?PORTABLE_LAVA_BUBBLE_CAPACITY:
            family==1u?PORTABLE_THWOMP_CAPACITY:family==2u?PORTABLE_CRAB_CAPACITY:family==3u?PORTABLE_SEAGULL_CAPACITY:family==4u?PORTABLE_CANNONBALL_CAPACITY:family==5u?PORTABLE_PIRANHA_CAPACITY:family==6u?PORTABLE_SNOWMAN_CAPACITY:family==7u?PORTABLE_PENGUIN_CAPACITY:family==8u?PORTABLE_BOO_CAPACITY:PORTABLE_COURSE_PROP_CAPACITY;
        if(actor_count>capacity)return 0;
        for(size_t i=0;i<actor_count;++i){
            PortableCollisionActor actor={.tag=(uint8_t)i};
            uint8_t flags,ordinal,size,size_z=0u;
            if(family==0u){
                const PortableLavaBubble *a=&world->lava_bubbles->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1004u;
                flags=a->flags;ordinal=a->placement_order;size=16u;
            }else if(family==1u){
                const PortableThwomp *a=&world->thwomps->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1002u;
                flags=a->flags;ordinal=a->placement_order;size=20u;
            }else if(family==2u){
                const PortableCrab *a=&world->crabs->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1003u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }else if(family==3u){
                const PortableSeagull *a=&world->seagulls->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x100cu;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }else if(family==4u){
                const PortableCannonball *a=&world->cannonballs->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1013u;
                flags=a->flags;ordinal=a->placement_order;size=a->size;
            }else if(family==5u){
                const PortablePiranha *a=&world->piranhas->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1009u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }
            else if(family==6u){
                const PortableSnowman *a=&world->snowmen->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1012u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }
            else if(family==7u){
                const PortablePenguin *a=&world->penguins->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1016u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }
            else if(family==8u){
                const PortableBoo *a=&world->boos->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1007u;
                flags=a->flags;ordinal=a->placement_order;size=36u;
            }else if(family==10u){
                const PortableMouse *a=&world->mice->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1015u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }else if(family==11u){
                const PortableThundercloud *a=&world->thunderclouds->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1010u;
                flags=a->flags;ordinal=a->placement_order;size=32u;
            }else if(family==12u){
                const PortableFallingStar *a=&world->falling_stars->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=0x1011u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
            }else{
                const PortableCourseProp *a=&world->course_props->actors[i];
                actor.pos_x=a->x_q16;actor.pos_y=a->y_q16;actor.pos_z=a->height_q16;
                actor.checkpoint_id=a->checkpoint;actor.source.type_flags=(a->source_code>>16u)==0xa0u?0x101bu:(a->source_code>>16u)==0xa1u?0x101cu:
                    (a->source_code>>16u)==0xa3u?0x1008u:(a->source_code>>16u)==3u?0x1018u:(a->source_code>>16u)==0xa2u?0x101du:0x1000u;
                flags=a->flags;ordinal=a->placement_order;size=8u;
                if((a->source_code>>16u)==0xa3u)size_z=32u;
                if((a->source_code>>16u)==0xa2u)size=size_z=4u;
            }
            if(!(flags&2u))continue;
            if(count==PORTABLE_COLLISION_MAX_PASSIVE){
                portable_diagnostic_printf("course=%u mode=%s passive course actors exceed %u\n",
                    world->course_id,world->mode_name,PORTABLE_COLLISION_MAX_PASSIVE);
                return 0;
            }
            actor.size_x=size;actor.size_y=size;actor.size_z=size_z?size_z:family==8u?64u:size;
            actors[count]=actor;order[count++]=ordinal;
        }
    }
    /* Source passive actors prepend in original placement order, irrespective
     * of family. A contact mutates the live driver's filter before the next. */
    for(size_t i=1;i<count;++i){
        const PortableCollisionActor actor=actors[i];const uint8_t ordinal=order[i];
        size_t j=i;
        while(j>0u && order[j-1u]<ordinal){actors[j]=actors[j-1u];order[j]=order[j-1u];--j;}
        actors[j]=actor;order[j]=ordinal;
    }
    for(size_t i=0;i<count;++i)
        if(portable_collision_add_passive(&list,&actors[i])!=PORTABLE_COLLISION_OK)return 0;
    if(portable_collision_collide(&list,world->checkpoint_count,
        PORTABLE_COLLISION_FAST)!=PORTABLE_COLLISION_OK)return 0;
    for(size_t i=0u;i<driver_count;++i)if(!contacts[i].ok)return 0;
    return 1;
}

int portable_course_contacts_collide(PortableCourseContacts *world)
{
    return portable_course_contacts_collide_many(world,1u);
}

