// Match SDL's embedded tri_texture DXIL, whose generated semantics differ
// from the adjacent HLSL source: color is TEXCOORD0, UV is TEXCOORD1.
// SPIR-V/Metal retain the same float4/float2 locations zero and one.
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> ui_texture : register(t0, space2);
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
SamplerState ui_sampler : register(s0, space2);
cbuffer UiUniforms : register(b0, space3)
{
    float4 quad;
    float4 rectangle;
    float4 shape;
    float4 effect;
    float4 paint_axis;
    float4 paint_positions;
    float4 paint_colors[4];
    float4 source;
    float4 auxiliary;
    float4 edges[160];
};
struct FragmentInput { float4 color : TEXCOORD0; float2 uv : TEXCOORD1; };

float4 paint(float2 pixel)
{
    float t = dot(pixel, paint_axis.xy) - paint_axis.z;
    uint count = (uint)clamp(paint_axis.w, 1.0, 4.0);
    if (count == 1 || t <= paint_positions[0]) return paint_colors[0];
    for (uint i = 1; i < count; ++i) {
        if (t <= paint_positions[i]) {
            float span = paint_positions[i] - paint_positions[i-1];
            float weight = span > 0.000001 ? saturate((t-paint_positions[i-1])/span) : 1.0;
            return floor(lerp(paint_colors[i-1], paint_colors[i], weight) + 0.5);
        }
    }
    return paint_colors[count-1];
}
float rounded_distance(float2 p, float2 half_size, float radius)
{
    float2 q = abs(p) - half_size + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}
float4 premultiplied(float4 bytes, float coverage)
{
    float alpha = floor(clamp(bytes.a * saturate(effect.x) * coverage, 0.0, 255.0) + 0.5);
    return float4(bytes.rgb * (alpha / 65025.0), alpha / 255.0);
}
// CPU checker first quantizes the half-resolution field, then filters bytes 3:1.
// Analytic coordinates avoid a full-window texture. GPU arithmetic is not a
// promise of identical CPU iterative-float checker boundaries.
float3 checker_texel(float2 index)
{
    float2 size = ceil(auxiliary.xy * 0.5) + 1.0;
    float unit = auxiliary.z * 0.5;
    float cell = 168.0 * unit, scroll = auxiliary.w * 26.0 * unit;
    float ca = cos(-0.33), sa = sin(-0.33), py = index.y + 0.5;
    float2 q = float2((index.x+0.5)*ca + py*sa + scroll,
                      -(index.x+0.5)*sa + py*ca + scroll*0.35) / cell + 1000.0;
    float2 tile = trunc(q), f = q-tile;
    float edge = min(min(f.x,1.0-f.x),min(f.y,1.0-f.y))*cell;
    float tone = ((int(tile.x)+int(tile.y)) & 1) ? 1.0 : 0.0;
    if (edge < 1.0) tone = 0.5 + (tone-0.5)*edge;
    float2 light_delta = float2(index.x-size.x*0.18,py+size.y*0.1);
    float light_radius = max(size.x,size.y)*1.1;
    float light = max(0.0,1.0-dot(light_delta,light_delta)/(light_radius*light_radius));
    float lift = tone*14.0+light*34.0;
    float fy = index.y/max(size.y-1.0,1.0);
    return floor(min(lerp(float3(52,190,246),float3(10,104,206),fy)+lift*float3(1,0.8,0.4),255.0));
}
float3 checker_row(float x, float y, float ny)
{
    return floor((checker_texel(float2(x,y))*3.0+checker_texel(float2(x,ny)))*0.25);
}
float4 checker(float2 pixel)
{
    int2 p = int2(floor(pixel));
    int2 s = p/2;
    int ny = (p.y & 1) ? s.y+1 : max(s.y-1,0);
    float3 a = checker_row(s.x,s.y,ny);
    if (p.x > 0) {
        int nx = (p.x & 1) ? s.x+1 : s.x-1;
        a = floor((3.0*a+checker_row(nx,s.y,ny))*0.25);
    }
    return float4(a/255.0,1.0);
}
float4 main(FragmentInput input) : SV_Target0
{
    float2 pixel = quad.xy + input.uv * quad.zw;
    uint kind = (uint)effect.w;
    if (kind == 10) return checker(pixel);
    float4 color = paint(pixel);
    float coverage = 1.0;
    if (kind == 1) {
        float distance = -1.0e30;
        uint count = (uint)clamp(effect.z,0.0,160.0);
        for (uint i=0;i<count;++i) distance=max(distance,dot(pixel,edges[i].xy)-edges[i].z);
        coverage=saturate(0.5-distance/max(shape.z,1.0));
    } else if (kind == 2 || kind == 5) {
        float2 half_size=rectangle.zw*0.5;
        float2 delta=pixel-(rectangle.xy+half_size);
        delta.x += shape.y*delta.y;
        float radius=clamp(shape.x,0.0,min(half_size.x,half_size.y));
        float distance=rounded_distance(delta,half_size,radius);
        coverage=kind==5 ? saturate(0.5-distance)*saturate(0.5+distance+shape.w) :
            saturate(0.5-distance/max(shape.z,1.0));
    } else if (kind == 3 || kind == 4) {
        float2 delta=pixel-rectangle.xy;
        float distance;
        if (kind==4) distance=abs(length(delta)-(rectangle.z-shape.w*0.5))-shape.w*0.5;
        else {
            float k0=length(delta/rectangle.zw),k1=length(delta/(rectangle.zw*rectangle.zw));
            distance=k1>0.000001 ? k0*(k0-1.0)/k1 : -min(rectangle.z,rectangle.w);
        }
        coverage=saturate(0.5-distance/(kind==4?1.0:max(shape.z,1.0)));
    } else if (kind == 6 || kind == 8) {
        float2 lo=floor(rectangle.xy+0.5),hi=floor(rectangle.xy+rectangle.zw+0.5);
        coverage=all(pixel>=lo)&&all(pixel<hi)?1.0:0.0;
    } else if (kind == 7) {
        float2 delta=pixel-rectangle.xy;
        float2 uv=source.xy+(delta/rectangle.zw)*source.zw;
        float4 sample_value=ui_texture.Sample(ui_sampler,uv);
        float sample_alpha=sample_value.a*255.0;
        if (sample_alpha<=0.5) return 0.0;
        float3 rgb=sample_value.rgb/sample_value.a*255.0;
        uint flags=(uint)effect.y;
        if ((flags & 4u)!=0) rgb=paint_colors[0].rgb;
        else {
            if ((flags & 2u)!=0) rgb=dot(rgb,float3(0.30,0.59,0.11))*0.55+30.0;
            rgb*=paint_colors[0].rgb/255.0;
        }
        color=float4(floor(clamp(rgb,0.0,255.0)),sample_alpha*paint_colors[0].a/255.0);
        float2 edge=saturate(delta+0.5)*saturate(rectangle.zw-delta+0.5);
        coverage=edge.x*edge.y;
        if (shape.x>0.0) coverage*=saturate(0.5-rounded_distance(delta-rectangle.zw*0.5,rectangle.zw*0.5,shape.x));
    } else if (kind == 9) {
        float2 uv=(pixel-rectangle.xy)/rectangle.zw;
        float value=ui_texture.Sample(ui_sampler,uv).r*255.0;
        float distance=(value-128.0)*(12.0/128.0)*auxiliary.x;
        coverage=saturate((distance+auxiliary.y)/auxiliary.z+0.5);
    }
    // Vertex color remains live in the SDL packed interface. Executors use white.
    return premultiplied(color,coverage)*input.color;
}
