#include <metal_stdlib>
using namespace metal;

struct CloudFrame
{
    float4x4 InvViewProj;
    float4x4 PrevViewProj;
    float4x4 ViewProj;
    float4 CameraPos;
    float4 CameraDelta;
    float4 SunDir;
    float4 SunColor;
    float4 AmbientTop;
    float4 AmbientBottom;
    float4 FogColor;
    float4 Layer;
    float4 Shape;
    float4 Erosion;
    float4 Wind;
    float4 Weather;
    float4 Albedo;
    float4 HistorySize;
    float4 ScreenSize;
    float4 Params;
    float4 Temporal;
    float4 DepthInfo;
    float4 Motion;
    float4 Shadow;
    float4 Advect;
};

constant float PI = 3.14159265;

#ifdef RML_ENTRY_FullscreenVS
vertex float4 FullscreenVS(uint id [[vertex_id]])
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
#endif

static float3 ViewRay(constant CloudFrame& f, float2 uv)
{
    float4 p = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0.5, 1) * f.InvViewProj;
    return normalize(p.xyz / p.w);
}

static bool IsSky(float encoded)
{
    return encoded >= 0;
}

static float DecodeT(float encoded)
{
    return encoded >= 0 ? encoded : -encoded - 1;
}

static float EncodeT(float transmittance, bool sky)
{
    return sky ? transmittance : -(1 + transmittance);
}

static float4 Load(texture2d<float> tex, int2 p)
{
    return tex.read(uint2(p));
}

static bool ClassBilinear(texture2d<float> tex, float2 uv, float2 size, bool sky, thread float4& value)
{
    float2 pos = uv * size - 0.5;
    int2 base = int2(floor(pos));
    float2 f = pos - float2(base);
    int2 limit = int2(size) - 1;
    float4 sum = 0;
    float weight = 0;
    for (int i = 0; i < 4; ++i)
    {
        int2 o = int2(i & 1, i >> 1);
        float4 c = Load(tex, clamp(base + o, int2(0), limit));
        if (IsSky(c.a) != sky)
            continue;
        float w = (o.x ? f.x : 1 - f.x) * (o.y ? f.y : 1 - f.y);
        sum += float4(c.rgb, DecodeT(c.a)) * w;
        weight += w;
    }
    value = weight > 1e-4 ? sum / weight : float4(0);
    return weight > 1e-4;
}

static bool NearestOfClass(texture2d<float> tex, float2 uv, float2 size, bool sky, thread float4& value)
{
    int2 limit = int2(size) - 1;
    int2 center = clamp(int2(uv * size), int2(0), limit);
    float best = 1e30;
    value = 0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float4 c = Load(tex, clamp(center + int2(x, y), int2(0), limit));
            float d = x * x + y * y;
            if (IsSky(c.a) == sky && d < best)
            {
                best = d;
                value = float4(c.rgb, DecodeT(c.a));
            }
        }
    }
    return best < 1e30;
}
