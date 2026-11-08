// A matched texture/sampler pair is one combined Vulkan descriptor. DXIL keeps
// its separate t0/s0 registers; Metal splits the combined pair at slot zero.
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> scene_texture : register(t0, space2);
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
SamplerState scene_sampler : register(s0, space2);
cbuffer Material : register(b0, space3)
{
    uint base_argb;
    uint tint_argb;
    uint cutoff_q16;
    uint alpha_mode;
    float2 uv_offset;
    // x: billboard hemispheric lighting; y: the instance's additive RGB.
    uint2 padding;
    // Texture matrix rows: u' = dot(uv_u.xyz, (u, v, 1)), likewise v'.
    float4 uv_u;
    float4 uv_v;
    // Lit materials: xyz is the mesh-local unit direction toward the light,
    // w is 1. shade = saturate(lit_base + lit_diffuse * max(0, N.L)).
    float4 light;
    float4 lit_base;
    float4 lit_diffuse;
    // Fog: rgb colour, a its strongest weight (0 disables fog).
    float4 fog_color;
    // near, far view depth; curve (0 linear, 1 ease-in, 2 ease-out,
    // 3 linear in 1/depth); texture filter (0 nearest, 1 bilinear, 2 three-point).
    float4 fog;
};
uint4 unpack_argb(uint value)
{
    return uint4((value >> 16) & 255u, (value >> 8) & 255u,
                 value & 255u, value >> 24);
}
// D3D stages link packed signatures, not just semantic names. Keep this order
// identical to the vertex output even though the pixel position is unused.
struct FragmentInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float4 shade : TEXCOORD2;
};
// Filtered materials read the four nearest texels through the nearest sampler
// (which keeps its wrap mode) and blend them bilinearly or, as the N64 does,
// across the three texels of the nearer triangle.
float4 filtered(float2 uv)
{
    uint width, height;
    scene_texture.GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 texel = uv * size - 0.5;
    const float2 f = frac(texel);
    const float2 base = (floor(texel) + 0.5) / size;
    const float2 step = 1.0 / size;
    const float4 c00 = scene_texture.Sample(scene_sampler, base);
    const float4 c10 = scene_texture.Sample(scene_sampler, base + float2(step.x, 0.0));
    const float4 c01 = scene_texture.Sample(scene_sampler, base + float2(0.0, step.y));
    const float4 c11 = scene_texture.Sample(scene_sampler, base + step);
    if (fog.w < 1.5)
        return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
    if (f.x + f.y <= 1.0)
        return c00 + f.x * (c10 - c00) + f.y * (c01 - c00);
    return c11 + (1.0 - f.x) * (c01 - c11) + (1.0 - f.y) * (c10 - c11);
}
float4 main(FragmentInput input) : SV_Target0
{
    // Recover nearest-sampled UNORM bytes, then preserve CPU truncation order:
    // floor(texel * vertex / 255), floor(* material / 255), floor(* tint / 255).
    // White vertices and the identity matrix leave stock results unchanged.
    const float3 source = float3(input.uv, 1.0);
    const float2 uv = float2(dot(uv_u.xyz, source), dot(uv_v.xyz, source)) + uv_offset;
    const float4 texel = fog.w > 0.5 ? filtered(uv) : scene_texture.Sample(scene_sampler, uv);
    uint4 color = (uint4)round(saturate(texel) * 255.0);
    color = color * (uint4)round(saturate(input.color) * 255.0) / 255u;
    color = color * unpack_argb(base_argb) / 255u;
    color = color * unpack_argb(tint_argb) / 255u;
    if (alpha_mode == 1u && color.a * 65536u < cutoff_q16 * 255u)
        discard;
    // Additive colour after the tint (the N64 combiner's texel + PRIM).
    color.rgb = min(color.rgb + unpack_argb(padding.y).rgb, 255u);
    // Optional billboard hemispheric lighting; preserve byte-exact default path.
    if (padding.x != 0u)
    {
        float light_scale = lerp(1.04, 0.78, saturate(input.uv.y));
        color.rgb = (uint3)round(saturate(float3(color.rgb) / 255.0 * light_scale * float3(1.03, 1.0, 0.96)) * 255.0);
    }
    if (light.w > 0.5)
    {
        const float3 normal = input.shade.xyz;
        const float facing = dot(normal, normal) > 1e-8 ? max(dot(normalize(normal), light.xyz), 0.0) : 0.0;
        const float3 shade = saturate(lit_base.rgb + lit_diffuse.rgb * facing);
        color.rgb = (uint3)round(float3(color.rgb) * shade);
    }
    if (fog_color.a > 0.0)
    {
        const float depth = input.shade.w;
        float t;
        if (fog.z > 2.5)
            t = saturate((1.0 / fog.x - 1.0 / max(depth, 1e-4)) / (1.0 / fog.x - 1.0 / fog.y));
        else
            t = saturate((depth - fog.x) / (fog.y - fog.x));
        if (fog.z > 1.5 && fog.z < 2.5)
            t = 1.0 - (1.0 - t) * (1.0 - t);
        else if (fog.z > 0.5 && fog.z < 1.5)
            t = t * t;
        color.rgb = (uint3)round(lerp(float3(color.rgb), fog_color.rgb * 255.0, t * fog_color.a));
    }
    // Blended fragments keep straight alpha; fully clear ones leave no trace.
    if (alpha_mode == 2u)
    {
        if (color.a == 0u)
            discard;
        return float4(color) / 255.0;
    }
    // Opaque and surviving cutout fragments write opaque depth-tested color.
    return float4(float3(color.rgb) / 255.0, 1.0);
}
