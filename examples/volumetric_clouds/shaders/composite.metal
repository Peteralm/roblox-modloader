constant float k_occluding_alpha = 0.5;

static float3 Tonemap(constant CloudFrame& f, float3 x)
{
    x *= f.SunColor.w;
    float3 shoulder = 0.6 + 0.4 * (1 - exp(-(x - 0.6) * 2.5));
    return select(shoulder, x, x < 0.6);
}

#ifdef RML_ENTRY_CompositePS
fragment float4 CompositePS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], texture2d<float> CloudColor [[texture(0)]])
{
    float2 uv = position.xy * f.ScreenSize.zw;
    float4 value;
#if RML_COMPOSITE_SKY
    if (!ClassBilinear(CloudColor, uv, f.HistorySize.xy, true, value))
    {
        if (!NearestOfClass(CloudColor, uv, f.HistorySize.xy, true, value))
        {
            float4 c = Load(CloudColor, clamp(int2(uv * f.HistorySize.xy), int2(0), int2(f.HistorySize.xy) - 1));
            value = float4(c.rgb, DecodeT(c.a));
        }
    }
#else
    if (!ClassBilinear(CloudColor, uv, f.HistorySize.xy, false, value))
    {
        if (!NearestOfClass(CloudColor, uv, f.HistorySize.xy, false, value))
            discard_fragment();
    }
#endif
    float alpha = saturate(1 - value.a);
    if (alpha < 0.002)
        discard_fragment();
    return float4(sqrt(saturate(Tonemap(f, value.rgb / alpha))), alpha);
}
#endif

struct CloudDepthOutput
{
    float depth [[depth(any)]];
};

#ifdef RML_ENTRY_CloudDepthPS
fragment CloudDepthOutput CloudDepthPS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], texture2d<float> CloudColor [[texture(0)]],
    texture2d<float> CloudFront [[texture(1)]], texture2d<float> SceneDist [[texture(2)]])
{
    float2 uv = position.xy * f.ScreenSize.zw;
    int2 hp = clamp(int2(uv * f.HistorySize.xy), int2(0), int2(f.HistorySize.xy) - 1);
    bool sky = Load(SceneDist, hp).r < 0;
    float4 value;
    if (!ClassBilinear(CloudColor, uv, f.HistorySize.xy, sky, value) || 1 - value.a < k_occluding_alpha)
        discard_fragment();
    float4 clip = float4(ViewRay(f, uv) * Load(CloudFront, hp).r, 1) * f.ViewProj;
    if (clip.w <= 0)
        discard_fragment();
    CloudDepthOutput output;
    output.depth = saturate(clip.z / clip.w);
    return output;
}
#endif

#ifdef RML_ENTRY_ShadowPS
fragment float4 ShadowPS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], texture2d<float> SceneDist [[texture(2)]], texture2d<float> ShadowMap [[texture(3)]])
{
    constexpr sampler shadow_sampler(filter::linear, address::clamp_to_edge);
    float2 uv = position.xy * f.ScreenSize.zw;
    float dist = Load(SceneDist, clamp(int2(uv * f.HistorySize.xy), int2(0), int2(f.HistorySize.xy) - 1)).r;
    if (dist < 0 || f.SunDir.y <= 0)
        discard_fragment();
    float3 world = f.CameraPos.xyz + ViewRay(f, uv) * dist;
    float lift = f.Layer.x - world.y;
    float2 suv = (world.xz + f.SunDir.xz / f.SunDir.y * max(lift, 0.0) - f.Shadow.xy) * f.Shadow.z + 0.5;
    float edge = saturate(min(min(suv.x, suv.y), min(1 - suv.x, 1 - suv.y)) * 10);
    float below = saturate(1 + lift * f.Layer.w);
    float shade = (1 - ShadowMap.sample(shadow_sampler, suv, level(0)).r) * edge * below * saturate(f.SunDir.y * 10) * f.Shadow.w;
    return float4(0, 0, 0, shade);
}
#endif
