Texture2D<float4> TraceColor : register(t0);
Texture2D<float2> TraceDist : register(t1);
Texture2D<float4> HistoryColor : register(t2);
Texture2D<float> SceneDist : register(t3);

struct ReconstructOutput
{
    float4 color : SV_Target0;
    float front : SV_Target1;
};

ReconstructOutput ReconstructPS(float4 position : SV_Position)
{
    int2 hp = int2(position.xy);
    bool sky = SceneDist.Load(int3(hp, 0)) < 0;
    int2 limit = int2(HistorySize.xy) - 1;
    float2 distance = TraceDist.Load(int3(hp, 0));

    float4 sum = 0;
    float4 sumSquares = 0;
    float count = 0;
    float4 filtered = 0;
    float filteredWeight = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float4 c = TraceColor.Load(int3(clamp(hp + int2(x, y), 0, limit), 0));
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

    float4 center = TraceColor.Load(int3(hp, 0));
    float4 current = filteredWeight > 0 ? filtered / filteredWeight : float4(center.rgb, DecodeT(center.a));
    float4 mean = count > 0 ? sum / count : current;
    float4 sigma = count > 0 ? sqrt(max(sumSquares / count - mean * mean, 0)) : 0;
    float4 lo = min(mean - sigma * 1.5 - 0.004, current);
    float4 hi = max(mean + sigma * 1.5 + 0.004, current);

    float4 result = current;
    if (Temporal.x > 0)
    {
        float2 here = (hp + 0.5) * HistorySize.zw;
        float4 clip = mul(PrevViewProj, float4(ViewRay(here) * distance.y + CameraDelta.xyz - Advect.xyz, 1));
        if (clip.w > 0)
        {
            float2 uv = float2(clip.x, -clip.y) / clip.w * 0.5 + 0.5;
            float4 history;
            if (all(uv >= 0) && all(uv <= 1) && ClassBilinear(HistoryColor, uv, HistorySize.xy, sky, history))
            {
                float motion = saturate(length((uv - here) * HistorySize.xy) / 8);
                result = lerp(clamp(history, lo, hi), current, lerp(Temporal.y, 0.75, motion));
            }
        }
    }
    ReconstructOutput output;
    output.color = float4(result.rgb, EncodeT(saturate(result.a), sky));
    output.front = distance.y;
    return output;
}
