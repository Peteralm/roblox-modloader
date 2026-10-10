#if RML_MSAA
typedef depth2d_ms<float> SceneDepthTexture;

static float LoadDepth(SceneDepthTexture depth, int2 p)
{
    return depth.read(uint2(p), 0);
}
#else
typedef depth2d<float> SceneDepthTexture;

static float LoadDepth(SceneDepthTexture depth, int2 p)
{
    return depth.read(uint2(p));
}
#endif

#ifdef RML_ENTRY_DepthPS
fragment float DepthPS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], SceneDepthTexture SceneDepth [[texture(0)]])
{
    int2 hp = int2(position.xy);
    int2 limit = int2(f.DepthInfo.xy) - 1;
    float farthest = -1;
    for (int i = 0; i < 4; ++i)
    {
        int2 p = min(hp * 2 + int2(i & 1, i >> 1), limit);
        float z = LoadDepth(SceneDepth, p);
        if (z <= 0)
            continue;
        float2 uv = (float2(p) + 0.5) * f.ScreenSize.zw;
        float4 w = float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1) * f.InvViewProj;
        farthest = max(farthest, length(w.xyz / w.w));
    }
    return farthest;
}
#endif
