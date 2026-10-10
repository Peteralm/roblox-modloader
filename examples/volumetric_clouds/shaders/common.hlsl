cbuffer CloudFrame : register(b2)
{
    row_major float4x4 InvViewProj;
    row_major float4x4 PrevViewProj;
    row_major float4x4 ViewProj;
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

static const float PI = 3.14159265;

float4 FullscreenVS(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 ViewRay(float2 uv)
{
    float4 p = mul(InvViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, 0.5, 1));
    return normalize(p.xyz / p.w);
}

bool IsSky(float encoded)
{
    return encoded >= 0;
}

float DecodeT(float encoded)
{
    return encoded >= 0 ? encoded : -encoded - 1;
}

float EncodeT(float transmittance, bool sky)
{
    return sky ? transmittance : -(1 + transmittance);
}

bool ClassBilinear(Texture2D<float4> tex, float2 uv, float2 size, bool sky, out float4 value)
{
    float2 pos = uv * size - 0.5;
    int2 base = int2(floor(pos));
    float2 f = pos - base;
    int2 limit = int2(size) - 1;
    float4 sum = 0;
    float weight = 0;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        int2 o = int2(i & 1, i >> 1);
        float4 c = tex.Load(int3(clamp(base + o, 0, limit), 0));
        if (IsSky(c.a) != sky)
            continue;
        float w = (o.x ? f.x : 1 - f.x) * (o.y ? f.y : 1 - f.y);
        sum += float4(c.rgb, DecodeT(c.a)) * w;
        weight += w;
    }
    value = weight > 1e-4 ? sum / weight : 0;
    return weight > 1e-4;
}

bool NearestOfClass(Texture2D<float4> tex, float2 uv, float2 size, bool sky, out float4 value)
{
    int2 limit = int2(size) - 1;
    int2 center = clamp(int2(uv * size), 0, limit);
    float best = 1e30;
    value = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float4 c = tex.Load(int3(clamp(center + int2(x, y), 0, limit), 0));
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
