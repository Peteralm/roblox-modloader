Texture3D<float> ShapeNoise : register(t0);
Texture3D<float> DetailNoise : register(t1);
Texture2D<float2> WeatherMap : register(t2);
Texture2D<float> SceneDist : register(t3);
SamplerState ShapeSampler : register(s0);
SamplerState DetailSampler : register(s1);
SamplerState WeatherSampler : register(s2);

float Remap(float v, float l0, float h0, float l1, float h1)
{
    return l1 + (v - l0) * (h1 - l1) / (h0 - l0);
}

float AltitudeAt(float t, float dy)
{
    return CameraPos.y + dy * t + t * t * (1 - dy * dy) * CameraPos.w;
}

bool AltitudeRoots(float altitude, float dy, out float t0, out float t1)
{
    t0 = 0;
    t1 = 0;
    float k = (1 - dy * dy) * CameraPos.w;
    float c = CameraPos.y - altitude;
    if (k < 1e-14)
    {
        if (abs(dy) < 1e-6)
            return false;
        t0 = -c / dy;
        t1 = t0;
        return true;
    }
    float disc = dy * dy - 4 * k * c;
    if (disc < 0)
        return false;
    float q = -0.5 * (dy + (dy >= 0 ? 1 : -1) * sqrt(disc));
    float r0 = q / k;
    float r1 = abs(q) > 1e-20 ? c / q : r0;
    t0 = min(r0, r1);
    t1 = max(r0, r1);
    return true;
}

bool RayLayer(float dy, out float tStart, out float tEnd)
{
    tStart = 0;
    tEnd = 0;
    float b0, b1, u0, u1;
    bool bottom = AltitudeRoots(Layer.x, dy, b0, b1);
    bool top = AltitudeRoots(Layer.y, dy, u0, u1);
    if (CameraPos.y < Layer.x)
    {
        if (!bottom || b1 <= 0)
            return false;
        tStart = b1;
        tEnd = top ? u1 : b1;
    }
    else if (CameraPos.y > Layer.y)
    {
        if (!top || u0 <= 0)
            return false;
        tStart = u0;
        tEnd = bottom && b0 > 0 ? b0 : u1;
    }
    else
    {
        tEnd = top ? u1 : 1e30;
        if (bottom && b0 > 0)
            tEnd = min(tEnd, b0);
    }
    return tEnd > tStart;
}

float HeightProfile(float hf, float type)
{
    const float4 stratus = float4(0.02, 0.05, 0.09, 0.11);
    const float4 stratocumulus = float4(0.02, 0.2, 0.48, 0.625);
    const float4 cumulus = float4(0.01, 0.0625, 0.78, 1.0);
    float4 g = type < 0.5 ? lerp(stratus, stratocumulus, type * 2) : lerp(stratocumulus, cumulus, type * 2 - 1);
    return smoothstep(g.x, g.y, hf) - smoothstep(g.z, g.w, hf);
}

static const float k_near_step = 60;
static const float k_fade_in = 120;
static const float k_light_step_max = 1200;
static const float k_light_extinction = 1.5;
static const float k_erosion_occlusion = 0.1;

struct CloudSample
{
    float density;
    float occlusion;
};

CloudSample SampleCloud(float3 pos, float hf, float mip, float detailMip, bool detail, bool light)
{
    CloudSample result;
    result.density = 0;
    result.occlusion = 1;
    float2 weather = WeatherMap.SampleLevel(WeatherSampler, (pos.xz - Weather.xy) * Weather.z, 0);
    float coverage = saturate(Shape.x * lerp(0.4, 1.6, weather.x));
    float profile = HeightProfile(hf, saturate(Erosion.z + (weather.y - 0.5) * 0.5));
    if (profile <= 0 || coverage <= 0)
        return result;
    float slant = pos.x / 3 + pos.z / 7;
    float3 sp = float3(pos.x - Wind.x - Motion.z * hf, pos.y + slant + Motion.x, pos.z - Wind.y - Motion.w * hf) * Shape.w;
    float n = lerp(1, ShapeNoise.SampleLevel(ShapeSampler, sp, mip), Shape.z);
    float base = saturate(Remap(n * profile, 1 - coverage, 1, 0, 1)) * coverage;
    if (base <= 0)
        return result;
    if (detail)
    {
        float3 dp = float3(pos.x - Wind.z, pos.y + Motion.y, pos.z - Wind.w) * Erosion.y;
        float d = DetailNoise.SampleLevel(DetailSampler, dp, detailMip);
        float erosion = lerp(d, 1 - d, saturate(hf * 4)) * Erosion.x * 0.35;
        base = saturate(Remap(base, erosion, 1, 0, 1));
        result.occlusion = saturate(1 - sqrt(erosion * k_erosion_occlusion));
    }
    else if (light)
    {
        base = max(base - Erosion.x * 0.035, 0);
    }
    result.density = base;
    return result;
}

float HenyeyGreenstein(float c, float g)
{
    float g2 = g * g;
    return (1 - g2) / (4 * PI * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5));
}

float Phase(float c, float scale)
{
    return lerp(HenyeyGreenstein(c, -0.3 * scale), HenyeyGreenstein(c, 0.8 * scale), 0.7);
}

float LightOpticalDepth(float3 pos, float hf)
{
    float dy = SunDir.y;
    float toEdge = dy >= 0 ? (1 - hf) * Layer.z / max(dy, 0.05) : hf * Layer.z / max(-dy, 0.05);
    int steps = (int)Params.z;
    float interval = (min(toEdge, steps * k_light_step_max) + 5) / steps;
    float depth = 0;
    [loop] for (int j = 0; j < steps; ++j)
    {
        float3 p = pos + SunDir.xyz * (interval * (0.25 + j));
        depth += SampleCloud(p, saturate((p.y - Layer.x) * Layer.w), 3.0 * j / steps, 0, false, true).density;
    }
    return depth * interval * Shape.y * k_light_extinction;
}

float2 Octahedral(float3 d)
{
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    return d.y >= 0 ? d.xz : (1 - abs(d.zx)) * (d.xz >= 0 ? 1 : -1);
}

float DirectionNoise(float3 dir)
{
    float2 p = floor(Octahedral(dir) * HistorySize.y * 1.3);
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

void March(float3 dir, float tStart, float tEnd, float jitter, inout float3 radiance, inout float transmittance, inout float meanDistance, inout float front)
{
    bool inside = tStart < k_near_step;
    int steps = (int)(Params.y * (inside ? 1.5 : 1));
    int octaves = (int)Params.w;
    float anchor = max(tStart, k_near_step);
    float curve = log2(1 + (tEnd - tStart) / anchor);
    float du = 1.0 / steps;
    float growth = exp2(curve * du) - 1;
    float cosTheta = dot(dir, SunDir.xyz);
    float phases[3];
    float scale = 1;
    [unroll] for (int o = 0; o < 3; ++o)
    {
        phases[o] = Phase(cosTheta, scale);
        scale *= Weather.w;
    }
    float3 sun = SunColor.rgb * PI;
    float u = du * jitter;
    float distanceSum = 0;
    float weightSum = 0;
    int empty = 0;
    bool coarse = true;
    [loop] for (int i = 0; i < steps * 2 && u < 1; ++i)
    {
        float offset = anchor * (exp2(curve * u) - 1);
        float t = tStart + offset;
        float stepLen = (offset + anchor) * growth;
        float altitude = AltitudeAt(t, dir.y);
        float3 pos = float3(CameraPos.x + dir.x * t, altitude, CameraPos.z + dir.z * t);
        float hf = saturate((altitude - Layer.x) * Layer.w);
        float fade = saturate(t / k_fade_in);
        float mip = saturate((t - 15000) / 150000) * 2.5;
        if (coarse)
        {
            if (SampleCloud(pos, hf, mip + 1, 0, false, false).density * fade > 0)
            {
                coarse = false;
                u = max(u - du, du * jitter);
                continue;
            }
            u += du * 2;
            continue;
        }
        CloudSample cloud = SampleCloud(pos, hf, mip, saturate((t - 10000) / 250000) * 4, true, false);
        float density = cloud.density * fade;
        if (density > 0.001)
        {
            empty = 0;
            float opticalDepth = LightOpticalDepth(pos, hf);
            float3 scatter = 0;
            float ms = 1;
            [unroll] for (int k = 0; k < 3; ++k)
            {
                if (k < octaves)
                    scatter += ms * exp(-opticalDepth * ms) * phases[k];
                ms *= Weather.w;
            }
            float powder = lerp(1, saturate(2 * (1 - exp(-4 * density))), smoothstep(0.5, -0.5, cosTheta) * Erosion.w);
            float3 ambient = lerp(AmbientBottom.rgb, AmbientTop.rgb, hf) * cloud.occlusion;
            float3 source = (sun * scatter * powder + ambient) * Albedo.rgb;
            float stepTransmittance = exp(-density * Shape.y * stepLen);
            float absorbed = transmittance * (1 - stepTransmittance);
            radiance += source * absorbed;
            distanceSum += t * absorbed;
            weightSum += absorbed;
            transmittance *= stepTransmittance;
            if (front < 0 && transmittance < 0.5)
                front = t;
            if (transmittance < 0.005)
                break;
        }
        else if (++empty >= 8)
        {
            coarse = true;
            empty = 0;
        }
        u += du;
    }
    if (weightSum > 0)
        meanDistance = distanceSum / weightSum;
}

struct TraceOutput
{
    float4 color : SV_Target0;
    float2 distance : SV_Target1;
};

TraceOutput TracePS(float4 position : SV_Position)
{
    int2 hp = min(int2(position.xy), int2(HistorySize.xy) - 1);
    float3 dir = ViewRay((hp + 0.5) * HistorySize.zw);
    float scene = SceneDist.Load(int3(hp, 0));
    bool sky = scene < 0;
    float3 radiance = 0;
    float transmittance = 1;
    float meanDistance = DepthInfo.z;
    float front = -1;
    float tStart, tEnd;
    if (RayLayer(dir.y, tStart, tEnd))
    {
        tEnd = min(tEnd, DepthInfo.z);
        if (!sky)
            tEnd = min(tEnd, scene);
        meanDistance = min(tStart, DepthInfo.z);
        if (tEnd > tStart)
            March(dir, tStart, tEnd, DirectionNoise(dir), radiance, transmittance, meanDistance, front);
    }
    float alpha = 1 - transmittance;
    float3 color = alpha > 1e-4 ? radiance / alpha : 0;
    float fade = exp(-meanDistance / FogColor.w);
    color = lerp(FogColor.rgb, color, fade);
    alpha *= saturate(fade * 1.5);
    TraceOutput output;
    output.color = float4(color * alpha, EncodeT(1 - alpha, sky));
    output.distance = float2(meanDistance, front < 0 ? meanDistance : front);
    return output;
}

static const float k_shadow_size = 256;
static const int k_shadow_steps = 12;

float ShadowMapPS(float4 position : SV_Position) : SV_Target
{
    float2 xz = Shadow.xy + (position.xy / k_shadow_size - 0.5) / Shadow.z;
    float stepLen = Layer.z / max(SunDir.y, 0.05) / k_shadow_steps;
    float depth = 0;
    [loop] for (int k = 0; k < k_shadow_steps; ++k)
    {
        float3 p = float3(xz.x, Layer.x, xz.y) + SunDir.xyz * (stepLen * (k + 0.5));
        depth += SampleCloud(p, saturate((p.y - Layer.x) * Layer.w), 0, 0, false, true).density;
    }
    return exp(-depth * stepLen * Shape.y);
}
