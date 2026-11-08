// World-only HD-2D finish. UV clamps keep split-screen panes independent.
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
Texture2D<float4> scene_color : register(t0, space2);
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
SamplerState color_sampler : register(s0, space2);
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
Texture2D<float> scene_depth : register(t1, space2);
#ifdef SPIRV
[[vk::combinedImageSampler]]
#endif
SamplerState depth_sampler : register(s1, space2);
cbuffer Finish : register(b0, space3)
{
    float4 pane;       // pixel x,y,w,h
    float4 target;     // inverse target w,h; near,far
    float4 effects;    // tilt shift,bloom,AO,grade
    float4 atmosphere;// haze; sky RGB
};
struct FragmentInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};
float2 bounded(float2 pixel)
{
    return clamp(pixel, pane.xy + 0.5, pane.xy + pane.zw - 0.5) * target.xy;
}
float distance_at(float2 pixel)
{
    float d = scene_depth.SampleLevel(depth_sampler, bounded(pixel), 0);
    return target.z * target.w / max(target.w - d * (target.w - target.z), 0.0001);
}
float4 main(FragmentInput input) : SV_Target0
{
    float2 p = input.position.xy;
    float2 local = (p - pane.xy) / pane.zw;
    float3 color = scene_color.SampleLevel(color_sampler, bounded(p), 0).rgb;
    float z = distance_at(p);
    // A generous sharp band includes kart, nearby hazards and the racing line.
    float blur = effects.x * max(smoothstep(0.82, 1.0, local.y) * 0.45,
        (1.0 - smoothstep(0.32, 0.60, local.y)) * smoothstep(18.0, 100.0, z));
    if (effects.x + effects.y > 0.0)
    {
        float3 soft = color, glow = max(color - 0.65, 0.0);
        // Deterministic two-ring disc: 12 taps, independent of frame timing.
        for (uint i = 0; i < 12; ++i)
        {
            float angle = i * 2.39996323;
            float radius = sqrt((i + 0.5) / 12.0);
            float2 q = p + float2(cos(angle), sin(angle)) * radius * max(pane.w * 0.012, 1.0);
            float3 sample_color = scene_color.SampleLevel(color_sampler, bounded(q), 0).rgb;
            soft += sample_color;
            glow += max(sample_color - 0.65, 0.0);
        }
        color = lerp(color, soft / 13.0, blur);
        color += glow * (effects.y * 0.55 / 13.0);
    }
    if (effects.z > 0.0 && z < target.w * 0.95)
    {
        float occlusion = 0.0;
        for (uint i = 0; i < 8; ++i)
        {
            float angle = i * 0.78539816;
            float dz = z - distance_at(p + float2(cos(angle), sin(angle)) * max(pane.w * 0.004, 1.0));
            // Reject large silhouette discontinuities instead of drawing dark halos.
            occlusion += smoothstep(0.04, 0.25, dz) * (1.0 - smoothstep(0.5, 2.5, dz));
        }
        color *= 1.0 - occlusion * (0.22 / 8.0);
    }
    if (atmosphere.x > 0.0 && z < target.w * 0.95)
    {
        float haze = (1.0 - exp(-z * 0.003)) * 0.32;
        color = lerp(color, lerp(atmosphere.yzw, float3(0.76, 0.82, 0.88), 0.45), haze);
    }
    if (effects.w > 0.0)
    {
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
        color = lerp(luminance.xxx, color, 1.12);
        color = (color - 0.5) * 1.06 + 0.5;
        color += float3(-0.012, 0.004, 0.022) * (1.0-luminance)
               + float3(0.028, 0.012, -0.014) * luminance;
    }
    return float4(saturate(color), 1.0);
}
