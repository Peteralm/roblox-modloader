#if RML_MSAA
Texture2DMS<float> SceneDepth : register(t0);

float LoadDepth(int2 p)
{
    return SceneDepth.Load(p, 0);
}
#else
Texture2D<float> SceneDepth : register(t0);

float LoadDepth(int2 p)
{
    return SceneDepth.Load(int3(p, 0));
}
#endif

float DepthPS(float4 position : SV_Position) : SV_Target
{
    int2 hp = int2(position.xy);
    int2 limit = int2(DepthInfo.xy) - 1;
    float farthest = -1;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        int2 p = min(hp * 2 + int2(i & 1, i >> 1), limit);
        float z = LoadDepth(p);
        if (z <= 0)
            continue;
        float2 uv = (p + 0.5) * ScreenSize.zw;
        float4 w = mul(InvViewProj, float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1));
        farthest = max(farthest, length(w.xyz / w.w));
    }
    return farthest;
}
