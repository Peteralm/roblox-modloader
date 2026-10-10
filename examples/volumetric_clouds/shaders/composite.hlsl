Texture2D<float4> CloudColor : register(t0);
Texture2D<float> CloudFront : register(t1);
Texture2D<float> SceneDist : register(t2);
Texture2D<float> ShadowMap : register(t3);
SamplerState ShadowSampler : register(s3);

static const float k_occluding_alpha = 0.5;

float3 Tonemap(float3 x)
{
    x *= SunColor.w;
    float3 shoulder = 0.6 + 0.4 * (1 - exp(-(x - 0.6) * 2.5));
    return x < 0.6 ? x : shoulder;
}

float4 CompositePS(float4 position : SV_Position) : SV_Target
{
    float2 uv = position.xy * ScreenSize.zw;
    float4 value;
#if RML_COMPOSITE_SKY
    if (!ClassBilinear(CloudColor, uv, HistorySize.xy, true, value))
    {
        if (!NearestOfClass(CloudColor, uv, HistorySize.xy, true, value))
        {
            float4 c = CloudColor.Load(int3(clamp(int2(uv * HistorySize.xy), 0, int2(HistorySize.xy) - 1), 0));
            value = float4(c.rgb, DecodeT(c.a));
        }
    }
#else
    if (!ClassBilinear(CloudColor, uv, HistorySize.xy, false, value))
    {
        if (!NearestOfClass(CloudColor, uv, HistorySize.xy, false, value))
            discard;
    }
#endif
    float alpha = saturate(1 - value.a);
    if (alpha < 0.002)
        discard;
    return float4(sqrt(saturate(Tonemap(value.rgb / alpha))), alpha);
}

float CloudDepthPS(float4 position : SV_Position) : SV_Depth
{
    float2 uv = position.xy * ScreenSize.zw;
    int2 hp = clamp(int2(uv * HistorySize.xy), 0, int2(HistorySize.xy) - 1);
    bool sky = SceneDist.Load(int3(hp, 0)) < 0;
    float4 value;
    if (!ClassBilinear(CloudColor, uv, HistorySize.xy, sky, value) || 1 - value.a < k_occluding_alpha)
        discard;
    float4 clip = mul(ViewProj, float4(ViewRay(uv) * CloudFront.Load(int3(hp, 0)), 1));
    if (clip.w <= 0)
        discard;
    return saturate(clip.z / clip.w);
}

float4 ShadowPS(float4 position : SV_Position) : SV_Target
{
    float2 uv = position.xy * ScreenSize.zw;
    float dist = SceneDist.Load(int3(clamp(int2(uv * HistorySize.xy), 0, int2(HistorySize.xy) - 1), 0));
    if (dist < 0 || SunDir.y <= 0)
        discard;
    float3 world = CameraPos.xyz + ViewRay(uv) * dist;
    float lift = Layer.x - world.y;
    float2 suv = (world.xz + SunDir.xz / SunDir.y * max(lift, 0) - Shadow.xy) * Shadow.z + 0.5;
    float edge = saturate(min(min(suv.x, suv.y), min(1 - suv.x, 1 - suv.y)) * 10);
    float below = saturate(1 + lift * Layer.w);
    float shade = (1 - ShadowMap.SampleLevel(ShadowSampler, suv, 0)) * edge * below * saturate(SunDir.y * 10) * Shadow.w;
    return float4(0, 0, 0, shade);
}
