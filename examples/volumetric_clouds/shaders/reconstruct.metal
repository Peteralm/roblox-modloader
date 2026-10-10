struct ReconstructOutput
{
    float4 color [[color(0)]];
    float front [[color(1)]];
};

#ifdef RML_ENTRY_ReconstructPS
fragment ReconstructOutput ReconstructPS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], texture2d<float> TraceColor [[texture(0)]],
    texture2d<float> TraceDist [[texture(1)]], texture2d<float> HistoryColor [[texture(2)]], texture2d<float> SceneDist [[texture(3)]])
{
    int2 hp = int2(position.xy);
    bool sky = Load(SceneDist, hp).r < 0;
    int2 limit = int2(f.HistorySize.xy) - 1;
    float2 distance = Load(TraceDist, hp).rg;

    float4 sum = 0;
    float4 sumSquares = 0;
    float count = 0;
    float4 filtered = 0;
    float filteredWeight = 0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float4 c = Load(TraceColor, clamp(hp + int2(x, y), int2(0), limit));
            if (IsSky(c.a) != sky)
                continue;
            float4 v = float4(c.rgb, DecodeT(c.a));
            float w = (x == 0 ? 2 : 1) * (y == 0 ? 2 : 1);
            sum += v;
            sumSquares += v * v;
            count += 1;
            filtered += v * w;
            filteredWeight += w;
        }
    }

    float4 center = Load(TraceColor, hp);
    float4 current = filteredWeight > 0 ? filtered / filteredWeight : float4(center.rgb, DecodeT(center.a));
    float4 mean = count > 0 ? sum / count : current;
    float4 sigma = count > 0 ? sqrt(max(sumSquares / count - mean * mean, 0)) : float4(0);
    float4 lo = min(mean - sigma * 1.5 - 0.004, current);
    float4 hi = max(mean + sigma * 1.5 + 0.004, current);

    float4 result = current;
    if (f.Temporal.x > 0)
    {
        float2 here = (float2(hp) + 0.5) * f.HistorySize.zw;
        float4 clip = float4(ViewRay(f, here) * distance.y + f.CameraDelta.xyz - f.Advect.xyz, 1) * f.PrevViewProj;
        if (clip.w > 0)
        {
            float2 uv = float2(clip.x, -clip.y) / clip.w * 0.5 + 0.5;
            float4 history;
            if (all(uv >= 0) && all(uv <= 1) && ClassBilinear(HistoryColor, uv, f.HistorySize.xy, sky, history))
            {
                float motion = saturate(length((uv - here) * f.HistorySize.xy) / 8);
                result = mix(clamp(history, lo, hi), current, mix(f.Temporal.y, 0.75, motion));
            }
        }
    }
    ReconstructOutput output;
    output.color = float4(result.rgb, EncodeT(saturate(result.a), sky));
    output.front = distance.y;
    return output;
}
#endif
