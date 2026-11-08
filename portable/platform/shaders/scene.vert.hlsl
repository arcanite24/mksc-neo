// Authored mesh scene shader. Presentation coordinates only; no simulation state.
cbuffer Camera : register(b0, space1)
{
    column_major float4x4 mvp;
};
struct VertexInput
{
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;
    float3 normal : TEXCOORD3;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    // Mesh-local normal and view depth (clip w) for lighting and fog.
    float4 shade : TEXCOORD2;
};
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    output.shade = float4(input.normal, output.position.w);
    return output;
}
