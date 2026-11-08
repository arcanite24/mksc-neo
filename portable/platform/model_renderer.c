#include "model_renderer.h"
#include "character_renderer.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

static int inverse_magnitude(int32_t value,uint16_t *out)
{
    const int64_t magnitude=value<0?-(int64_t)value:value;
    if(magnitude>UINT16_MAX)return 0;
    *out=(uint16_t)(magnitude?magnitude:256);
    return 1;
}

static double yaw_sample(const int32_t *sine,uint16_t yaw,unsigned phase)
{
    const unsigned index=((unsigned)yaw/64u+phase)&1023u;
    const double fraction=(double)(yaw&63u)/64.0;
    return ((double)sine[index]+((double)sine[(index+1u)&1023u]-sine[index])*fraction)/32768.0;
}

static size_t texture_coordinate(double value,size_t extent,int wrap)
{
    if(wrap)value-=floor(value);
    else if(value<=0.0)return 0u;
    else if(value>=1.0)return extent-1u;
    const size_t coordinate=(size_t)(value*(double)extent);
    return coordinate<extent?coordinate:extent-1u;
}

static uint32_t material_sample(const void *userdata,double u,double v)
{
    const PortableModelMaterial *material=userdata;
    uint32_t color=material->argb;
    if(material->texture.pixels){
        const size_t x=texture_coordinate(u,material->texture.width,material->flags&PORTABLE_MODEL_WRAP_U);
        const size_t y=texture_coordinate(v,material->texture.height,material->flags&PORTABLE_MODEL_WRAP_V);
        const uint32_t texel=portable_native_image_pixel(&material->texture,x,y);
        color=0u;
        for(unsigned shift=0;shift<32u;shift+=8u)
            color|=(((texel>>shift)&255u)*((material->argb>>shift)&255u)/255u)<<shift;
    }
    return (color>>24u)>=128u?color|UINT32_C(0xff000000):0u;
}

static int material_valid(const PortableModelMaterial *material)
{
    if(material->flags&~(uint32_t)(PORTABLE_MODEL_WRAP_U|PORTABLE_MODEL_WRAP_V))return 0;
    if(!material->texture.pixels)return 1;
    const PortableNativeImage *texture=&material->texture;
    return texture->width && texture->height && texture->width<=PORTABLE_MODEL_MAX_TEXTURE_EXTENT &&
        texture->height<=PORTABLE_MODEL_MAX_TEXTURE_EXTENT &&
        (!texture->palette || (texture->palette_count &&
            (unsigned)texture->palette_base+texture->palette_count<=256u));
}

static int pose_effects(const PortableModelPose *pose,const PortableSoftwareFrame *frame,
    const PortableDriverProjectionRules *rules,const PortableCameraProjectionResult *ground,
    double *scale_x,double *scale_y,double *bob)
{
    uint16_t ix,iy;
    if(!inverse_magnitude(pose->inverse_x_q8,&ix)||!inverse_magnitude(pose->inverse_y_q8,&iy))return 0;
    const uint16_t base=pose->base_inverse_q8?pose->base_inverse_q8:256u;
    *scale_x=65536.0/((double)base*ix);
    *scale_y=65536.0/((double)base*iy);
    *bob=0.0;
    if(pose->source_driver){
        PortableCharacterProjectionGeometry effect,normal;
        if(!portable_character_projection_geometry(frame,rules,ground,base,ix,iy,pose->bob_offset,&effect)||
            !portable_character_projection_geometry(frame,rules,ground,256u,256u,256u,0,&normal)||
            !effect.size||!normal.size)return 0;
        *scale_x=(double)normal.inverse_x/effect.inverse_x;
        *scale_y=(double)normal.inverse_y/effect.inverse_y;
        *bob=(double)effect.bob_delta/(double)portable_software_frame_scale(frame);
    }
    if(pose->inverse_x_q8<0)*scale_x=-*scale_x;
    if(pose->inverse_y_q8<0)*scale_y=-*scale_y;
    return 1;
}

static int triangle_vertices(const PortableModelView *mesh,const PortableScene3DVertex *vertices,
    size_t index,PortableScene3DVertex triangle[3],uint16_t *material)
{
    uint16_t indices[3];
    if(!portable_model_triangle(mesh,index,indices,material)||*material>=mesh->material_count)return 0;
    for(unsigned i=0;i<3u;++i){
        if(indices[i]>=mesh->vertex_count)return 0;
        triangle[i]=vertices[indices[i]];
    }
    return 1;
}

int portable_model_render(PortableScene3D *target,const PortableModelBinding *binding,
    const PortableModelPose *pose,const PortableCameraState *camera,
    const PortableCameraProjection *basis,const PortableScene3DProjection *projection,
    const PortableDriverProjectionRules *driver_projection,const int32_t *sine,size_t sine_count)
{
    if(!target||!target->frame||!target->depth||!target->billboard_depth||!binding||!pose||
        !camera||camera->elevation_q16<=0||!basis||!projection||!sine||sine_count<1024u||
        pose->source_driver>1u)return 0;
    const PortableModelView *mesh=&binding->mesh;
    if(!mesh->vertices||!mesh->triangles||!mesh->vertex_count||mesh->vertex_count>PORTABLE_MODEL_MAX_VERTICES||
        !mesh->triangle_count||mesh->triangle_count>PORTABLE_MODEL_MAX_TRIANGLES||
        !mesh->material_count||mesh->material_count>PORTABLE_MODEL_MAX_MATERIALS)return 0;
    for(unsigned i=0;i<3u;++i)if(binding->scale_q16[i]==0)return 0;
    for(size_t i=0;i<mesh->material_count;++i)if(!material_valid(&mesh->materials[i]))return 0;
    PortableCameraProjectionResult source;
    PortableCameraProjectionPreciseResult precise;
    if(portable_camera_projection_project(camera,basis,pose->position_x_q16,pose->position_y_q16,0,&source)||
        portable_camera_projection_project_precise(camera,basis,pose->position_x_q16,pose->position_y_q16,0,&precise))return 0;
    double scale_x,scale_y,bob;
    if(!pose_effects(pose,target->frame,driver_projection,&source,&scale_x,&scale_y,&bob))return 0;
    const double actor_z=(double)precise.distance_q16/65536.0;
    const double offset=camera->projection_y_offset_precise?
        (double)camera->projection_y_offset_q16/65536.0:camera->projection_y_offset;
    const double actor_x=((double)precise.x_q16/65536.0-camera->screen_x)*actor_z/1024.0;
    const double actor_y=-((double)precise.y_q16/65536.0-camera->screen_y-offset)*actor_z/1024.0;
    const double elevation=(double)camera->elevation_q16/65536.0;
    const double translation=bob+(double)pose->screen_y_offset_q16/65536.0-
        (double)pose->height_q16/65536.0*1024.0/actor_z;
    const double sine_yaw=yaw_sample(sine,pose->visual_yaw,0u),cosine_yaw=yaw_sample(sine,pose->visual_yaw,256u);
    PortableScene3DVertex *vertices=malloc(mesh->vertex_count*sizeof *vertices);
    if(!vertices)return 0;
    int ok=0;
    for(size_t i=0;i<mesh->vertex_count;++i){
        int32_t xyz[3],uv[2];
        if(!portable_model_vertex(mesh,i,xyz,uv))goto done;
        double local[3];
        for(unsigned axis=0;axis<3u;++axis)
            local[axis]=((double)xyz[axis]-binding->pivot_q16[axis])/65536.0*
                ((double)binding->scale_q16[axis]/65536.0);
        const double rotated[3]={local[0]*cosine_yaw-local[1]*sine_yaw,
            local[0]*sine_yaw+local[1]*cosine_yaw,local[2]};
        double p[3]={0.0,0.0,0.0};
        for(unsigned axis=0;axis<3u;++axis)for(unsigned row=0;row<3u;++row)
            p[axis]+=(double)basis->basis[row][axis]/65536.0*rotated[row];
        const double z=actor_z-p[2]*1024.0/elevation;
        /* Source hop/bob are rigid screen translations; squash acts around
         * the projected authored foot, never changing geometric depth. */
        const double pivot_x=actor_x/actor_z*z,pivot_y=actor_y/actor_z*z;
        vertices[i]=(PortableScene3DVertex){pivot_x+(actor_x+p[0]-pivot_x)*scale_x,
            pivot_y+(actor_y-p[1]-pivot_y)*scale_y-translation*z/1024.0,
            z,(double)uv[0]/65536.0,(double)uv[1]/65536.0};
    }
    /* Preflight clipping as well as wire references. No failed model can
     * leave a partly rendered replacement behind its billboard fallback. */
    for(size_t i=0;i<mesh->triangle_count;++i){
        PortableScene3DVertex triangle[3];uint16_t material;
        if(!triangle_vertices(mesh,vertices,i,triangle,&material)||
            !portable_scene_3d_triangle_validate(target,projection,triangle))goto done;
    }
    {
        const uint8_t previous=target->model_pass;
        target->model_pass=1u;
        for(size_t i=0;i<mesh->triangle_count;++i){
            PortableScene3DVertex triangle[3];uint16_t material=0u;
            (void)triangle_vertices(mesh,vertices,i,triangle,&material);
            (void)portable_scene_3d_triangle(target,projection,triangle,material_sample,&mesh->materials[material]);
        }
        target->model_pass=previous;
    }
    ok=1;
done:
    free(vertices);return ok;
}
