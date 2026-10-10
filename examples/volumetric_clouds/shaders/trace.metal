struct CloudTextures
{
    texture3d<float> shape;
    texture3d<float> detail;
    texture2d<float> weather;
};

constexpr sampler noise_sampler(filter::linear, mip_filter::linear, address::repeat);

static float Remap(float v, float l0, float h0, float l1, float h1)
{
    return l1 + (v - l0) * (h1 - l1) / (h0 - l0);
}

static float AltitudeAt(constant CloudFrame& f, float t, float dy)
{
    return f.CameraPos.y + dy * t + t * t * (1 - dy * dy) * f.CameraPos.w;
}

static bool AltitudeRoots(constant CloudFrame& f, float altitude, float dy, thread float& t0, thread float& t1)
{
    t0 = 0;
    t1 = 0;
    float k = (1 - dy * dy) * f.CameraPos.w;
    float c = f.CameraPos.y - altitude;
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

static bool RayLayer(constant CloudFrame& f, float dy, thread float& tStart, thread float& tEnd)
{
    tStart = 0;
    tEnd = 0;
    float b0, b1, u0, u1;
    bool bottom = AltitudeRoots(f, f.Layer.x, dy, b0, b1);
    bool top = AltitudeRoots(f, f.Layer.y, dy, u0, u1);
    if (f.CameraPos.y < f.Layer.x)
    {
        if (!bottom || b1 <= 0)
            return false;
        tStart = b1;
        tEnd = top ? u1 : b1;
    }
    else if (f.CameraPos.y > f.Layer.y)
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

static float HeightProfile(float hf, float type)
{
    const float4 stratus = float4(0.02, 0.05, 0.09, 0.11);
    const float4 stratocumulus = float4(0.02, 0.2, 0.48, 0.625);
    const float4 cumulus = float4(0.01, 0.0625, 0.78, 1.0);
    float4 g = type < 0.5 ? mix(stratus, stratocumulus, type * 2) : mix(stratocumulus, cumulus, type * 2 - 1);
    return smoothstep(g.x, g.y, hf) - smoothstep(g.z, g.w, hf);
}

constant float k_near_step = 60;
constant float k_fade_in = 120;
constant float k_light_step_max = 1200;
constant float k_light_extinction = 1.5;
constant float k_erosion_occlusion = 0.1;

struct CloudSample
{
    float density;
    float occlusion;
};

static CloudSample SampleCloud(constant CloudFrame& f, CloudTextures tex, float3 pos, float hf, float mip, float detailMip, bool detail, bool light)
{
    CloudSample result;
    result.density = 0;
    result.occlusion = 1;
    float2 weather = tex.weather.sample(noise_sampler, (pos.xz - f.Weather.xy) * f.Weather.z, level(0)).rg;
    float coverage = saturate(f.Shape.x * mix(0.4, 1.6, weather.x));
    float profile = HeightProfile(hf, saturate(f.Erosion.z + (weather.y - 0.5) * 0.5));
    if (profile <= 0 || coverage <= 0)
        return result;
    float slant = pos.x / 3 + pos.z / 7;
    float3 sp = float3(pos.x - f.Wind.x - f.Motion.z * hf, pos.y + slant + f.Motion.x, pos.z - f.Wind.y - f.Motion.w * hf) * f.Shape.w;
    float n = mix(1.0, tex.shape.sample(noise_sampler, sp, level(mip)).r, f.Shape.z);
    float base = saturate(Remap(n * profile, 1 - coverage, 1, 0, 1)) * coverage;
    if (base <= 0)
        return result;
    if (detail)
    {
        float3 dp = float3(pos.x - f.Wind.z, pos.y + f.Motion.y, pos.z - f.Wind.w) * f.Erosion.y;
        float d = tex.detail.sample(noise_sampler, dp, level(detailMip)).r;
        float erosion = mix(d, 1 - d, saturate(hf * 4)) * f.Erosion.x * 0.35;
        base = saturate(Remap(base, erosion, 1, 0, 1));
        result.occlusion = saturate(1 - sqrt(erosion * k_erosion_occlusion));
    }
    else if (light)
    {
        base = max(base - f.Erosion.x * 0.035, 0.0);
    }
    result.density = base;
    return result;
}

static float HenyeyGreenstein(float c, float g)
{
    float g2 = g * g;
    return (1 - g2) / (4 * PI * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5));
}

static float Phase(float c, float scale)
{
    return mix(HenyeyGreenstein(c, -0.3 * scale), HenyeyGreenstein(c, 0.8 * scale), 0.7);
}

static float LightOpticalDepth(constant CloudFrame& f, CloudTextures tex, float3 pos, float hf)
{
    float dy = f.SunDir.y;
    float toEdge = dy >= 0 ? (1 - hf) * f.Layer.z / max(dy, 0.05) : hf * f.Layer.z / max(-dy, 0.05);
    int steps = int(f.Params.z);
    float interval = (min(toEdge, steps * k_light_step_max) + 5) / steps;
    float depth = 0;
    for (int j = 0; j < steps; ++j)
    {
        float3 p = pos + f.SunDir.xyz * (interval * (0.25 + j));
        depth += SampleCloud(f, tex, p, saturate((p.y - f.Layer.x) * f.Layer.w), 3.0 * j / steps, 0, false, true).density;
    }
    return depth * interval * f.Shape.y * k_light_extinction;
}

static float2 Octahedral(float3 d)
{
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    return d.y >= 0 ? d.xz : (1 - abs(d.zx)) * select(float2(-1), float2(1), d.xz >= 0);
}

static float DirectionNoise(constant CloudFrame& f, float3 dir)
{
    float2 p = floor(Octahedral(dir) * f.HistorySize.y * 1.3);
    return fract(52.9829189 * fract(dot(p, float2(0.06711056, 0.00583715))));
}

static void March(constant CloudFrame& f, CloudTextures tex, float3 dir, float tStart, float tEnd, float jitter, thread float3& radiance, thread float& transmittance,
    thread float& meanDistance, thread float& front)
{
    bool inside = tStart < k_near_step;
    int steps = int(f.Params.y * (inside ? 1.5 : 1));
    int octaves = int(f.Params.w);
    float anchor = max(tStart, k_near_step);
    float curve = log2(1 + (tEnd - tStart) / anchor);
    float du = 1.0 / steps;
    float growth = exp2(curve * du) - 1;
    float cosTheta = dot(dir, f.SunDir.xyz);
    float phases[3];
    float scale = 1;
    for (int o = 0; o < 3; ++o)
    {
        phases[o] = Phase(cosTheta, scale);
        scale *= f.Weather.w;
    }
    float3 sun = f.SunColor.rgb * PI;
    float u = du * jitter;
    float distanceSum = 0;
    float weightSum = 0;
    int empty = 0;
    bool coarse = true;
    for (int i = 0; i < steps * 2 && u < 1; ++i)
    {
        float offset = anchor * (exp2(curve * u) - 1);
        float t = tStart + offset;
        float stepLen = (offset + anchor) * growth;
        float altitude = AltitudeAt(f, t, dir.y);
        float3 pos = float3(f.CameraPos.x + dir.x * t, altitude, f.CameraPos.z + dir.z * t);
        float hf = saturate((altitude - f.Layer.x) * f.Layer.w);
        float fade = saturate(t / k_fade_in);
        float mip = saturate((t - 15000) / 150000) * 2.5;
        if (coarse)
        {
            if (SampleCloud(f, tex, pos, hf, mip + 1, 0, false, false).density * fade > 0)
            {
                coarse = false;
                u = max(u - du, du * jitter);
                continue;
            }
            u += du * 2;
            continue;
        }
        CloudSample cloud = SampleCloud(f, tex, pos, hf, mip, saturate((t - 10000) / 250000) * 4, true, false);
        float density = cloud.density * fade;
        if (density > 0.001)
        {
            empty = 0;
            float opticalDepth = LightOpticalDepth(f, tex, pos, hf);
            float3 scatter = 0;
            float ms = 1;
            for (int k = 0; k < 3; ++k)
            {
                if (k < octaves)
                    scatter += ms * exp(-opticalDepth * ms) * phases[k];
                ms *= f.Weather.w;
            }
            float powder = mix(1.0, saturate(2 * (1 - exp(-4 * density))), smoothstep(0.5, -0.5, cosTheta) * f.Erosion.w);
            float3 ambient = mix(f.AmbientBottom.rgb, f.AmbientTop.rgb, hf) * cloud.occlusion;
            float3 source = (sun * scatter * powder + ambient) * f.Albedo.rgb;
            float stepTransmittance = exp(-density * f.Shape.y * stepLen);
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
    float4 color [[color(0)]];
    float2 distance [[color(1)]];
};

#ifdef RML_ENTRY_TracePS
fragment TraceOutput TracePS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], texture3d<float> ShapeNoise [[texture(0)]],
    texture3d<float> DetailNoise [[texture(1)]], texture2d<float> WeatherMap [[texture(2)]], texture2d<float> SceneDist [[texture(3)]])
{
    CloudTextures tex{ShapeNoise, DetailNoise, WeatherMap};
    int2 hp = min(int2(position.xy), int2(f.HistorySize.xy) - 1);
    float3 dir = ViewRay(f, (float2(hp) + 0.5) * f.HistorySize.zw);
    float scene = Load(SceneDist, hp).r;
    bool sky = scene < 0;
    float3 radiance = 0;
    float transmittance = 1;
    float meanDistance = f.DepthInfo.z;
    float front = -1;
    float tStart, tEnd;
    if (RayLayer(f, dir.y, tStart, tEnd))
    {
        tEnd = min(tEnd, f.DepthInfo.z);
        if (!sky)
            tEnd = min(tEnd, scene);
        meanDistance = min(tStart, f.DepthInfo.z);
        if (tEnd > tStart)
            March(f, tex, dir, tStart, tEnd, DirectionNoise(f, dir), radiance, transmittance, meanDistance, front);
    }
    float alpha = 1 - transmittance;
    float3 color = alpha > 1e-4 ? radiance / alpha : float3(0);
    float fade = exp(-meanDistance / f.FogColor.w);
    color = mix(f.FogColor.rgb, color, fade);
    alpha *= saturate(fade * 1.5);
    TraceOutput output;
    output.color = float4(color * alpha, EncodeT(1 - alpha, sky));
    output.distance = float2(meanDistance, front < 0 ? meanDistance : front);
    return output;
}
#endif

constant float k_shadow_size = 256;
constant int k_shadow_steps = 12;

#ifdef RML_ENTRY_ShadowMapPS
fragment float ShadowMapPS(float4 position [[position]], constant CloudFrame& f [[buffer(2)]], texture3d<float> ShapeNoise [[texture(0)]],
    texture3d<float> DetailNoise [[texture(1)]], texture2d<float> WeatherMap [[texture(2)]])
{
    CloudTextures tex{ShapeNoise, DetailNoise, WeatherMap};
    float2 xz = f.Shadow.xy + (position.xy / k_shadow_size - 0.5) / f.Shadow.z;
    float stepLen = f.Layer.z / max(f.SunDir.y, 0.05) / k_shadow_steps;
    float depth = 0;
    for (int k = 0; k < k_shadow_steps; ++k)
    {
        float3 p = float3(xz.x, f.Layer.x, xz.y) + f.SunDir.xyz * (stepLen * (k + 0.5));
        depth += SampleCloud(f, tex, p, saturate((p.y - f.Layer.x) * f.Layer.w), 0, 0, false, true).density;
    }
    return exp(-depth * stepLen * f.Shape.y);
}
#endif
