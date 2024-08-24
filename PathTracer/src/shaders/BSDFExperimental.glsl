#ifndef BSDFFile
#define BSDFFile

#include "raycommon.glsl"

struct BSDFSampleData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  RayDir;       // [out] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BSDF;         // [out] BSDF
};

struct RayInfo
{
    vec3 dir;
    float theta;
    float sinTheta;
    float tanTheta;
    float cosTheta;
    float alpha;
    float Lambda;

    float h;
    float C1;
    float G1;
};

void UpdateDirection(vec3 w, inout RayInfo rayInfo, float ax, float ay)
{
    rayInfo.dir = w;

    rayInfo.theta = acos(w.z);
    rayInfo.cosTheta = w.z;
    rayInfo.sinTheta = sin(rayInfo.theta);
    rayInfo.tanTheta = rayInfo.sinTheta / rayInfo.cosTheta;
    const float invSinTheta2 = 1.0f / (1.0f - w.z * w.z);
    const float cosPhi2 = w.x * w.x * invSinTheta2;
    const float sinPhi2 = w.y * w.y * invSinTheta2;
    rayInfo.alpha = sqrt(cosPhi2 * ax * ax + sinPhi2 * ay * ay);

    // Lambda
    if (w.z > 0.9999f)
        rayInfo.Lambda = 0.0f;
    else if (w.z < -0.9999f)
        rayInfo.Lambda = -1.0f;
    else
    {
        const float a = 1.0f / rayInfo.tanTheta / rayInfo.alpha;
        rayInfo.Lambda = 0.5f * (-1.0f + ((a > 0) ? 1.0f : -1.0f) * sqrt(1 + 1 / (a * a)));
    }
}

void UpdateHeight(in float h, inout RayInfo rayInfo)
{
    rayInfo.h = h;
    rayInfo.C1 = min(1.0f, max(0.0f, 0.5f * (h + 1.0f)));

    if (rayInfo.dir.z > 0.9999f)
        rayInfo.G1 = 1.0f;
    else if (rayInfo.dir.z <= 0.0f)
        rayInfo.G1 = 0.0f;
    else
        rayInfo.G1 = pow(rayInfo.C1, rayInfo.Lambda);
}

float InvC1(float U)
{
    const float h = max(-1.0f, min(1.0f, 2.0f * U - 1.0f));
    return h;
}

float SampleHeight(in RayInfo ray, float U)
{
    if (ray.dir.z > 0.9999f)
        return FLT_MAX;
    if (ray.dir.z < -0.9999f)
    {
        const float value = InvC1(U * ray.C1);
        return value;
    }
    if (abs(ray.dir.z) < 0.0001f)
        return ray.h;

    // probability of intersection
    if (U > 1.0f - ray.G1) // leave the microsurface
        return FLT_MAX;

    const float h = InvC1(
        ray.C1 / pow((1.0f - U), 1.0f / ray.Lambda)
    );
    return h;
}

vec2 SampleP22_11(float theta_i, float U, float U_2, float alpha_x, float alpha_y)
{
    vec2 slope;

    if (theta_i < 0.0001f)
    {
        const float r = sqrt(U / (1.0f - U));
        const float phi = 6.28318530718f * U_2;
        slope.x = r * cos(phi);
        slope.y = r * sin(phi);
        return slope;
    }

    // constant
    const float sin_theta_i = sin(theta_i);
    const float cos_theta_i = cos(theta_i);
    const float tan_theta_i = sin_theta_i / cos_theta_i;

    // slope associated to theta_i
    const float slope_i = cos_theta_i / sin_theta_i;

    // projected area
    const float projectedarea = 0.5f * (cos_theta_i + 1.0f);
    if (projectedarea < 0.0001f || projectedarea != projectedarea)
        return vec2(0, 0);
    // normalization coefficient
    const float c = 1.0f / projectedarea;

    const float A = 2.0f * U / cos_theta_i / c - 1.0f;
    const float B = tan_theta_i;
    const float tmp = 1.0f / (A * A - 1.0f);

    const float D = sqrt(max(0.0f, B * B * tmp * tmp - (A * A - B * B) * tmp));
    const float slope_x_1 = B * tmp - D;
    const float slope_x_2 = B * tmp + D;
    slope.x = (A < 0.0f || slope_x_2 > 1.0f / tan_theta_i) ? slope_x_1 : slope_x_2;

    float U2;
    float S;
    if (U_2 > 0.5f)
    {
        S = 1.0f;
        U2 = 2.0f * (U_2 - 0.5f);
    }
    else
    {
        S = -1.0f;
        U2 = 2.0f * (0.5f - U_2);
    }
    const float z = (U2 * (U2 * (U2 * 0.27385f - 0.73369f) + 0.46341f)) / (U2 * (U2 * (U2 * 0.093073f + 0.309420f) - 1.000000f) + 0.597999f);
    slope.y = S * z * sqrt(1.0f + slope.x * slope.x);

    return slope;
}

vec3 SamplePhaseFunctionConductor(inout uint seed, in vec3 wi, in Material mat, out vec3 weight)
{
    const float U1 = Rnd(seed);
    const float U2 = Rnd(seed);

    // sample D_wi
    // stretch to match configuration with alpha=1.0	
    const vec3 wi_11 = normalize(vec3(mat.ax * wi.x, mat.ay * wi.y, wi.z));

    // sample visible slope with alpha=1.0
    vec2 slope_11 = SampleP22_11(acos(wi_11.z), U1, U2, mat.ax, mat.ay);

    // align with view direction
    const float phi = atan(wi_11.y, wi_11.x);
    vec2 slope = vec2(cos(phi) * slope_11.x - sin(phi) * slope_11.y, sin(phi) * slope_11.x + cos(phi) * slope_11.y);

    // stretch back
    slope.x *= mat.ax;
    slope.y *= mat.ay;

    // compute normal
    vec3 wm;
    // if numerical instability
    if ((slope.x != slope.x) || !IsFiniteNumber(slope.x))
    {
        if (wi.z > 0) wm = vec3(0.0f, 0.0f, 1.0f);
        else wm = normalize(vec3(wi.x, wi.y, 0.0f));
    }
    else
        wm = normalize(vec3(-slope.x, -slope.y, 1.0f));

    // reflect
    const vec3 wo = -wi + 2.0f * wm * dot(wi, wm);
    //weight = Fresnel(dot(wi, wm), mat.Color.xyz);
    weight = mat.Color.xyz; // Don't compute fresnel so that color is always tinted for conductors which I assume is correct?

    return wo;
}

vec3 SampleConductorDirection(inout uint seed, in vec3 wi, in Material mat, int maxBounces, out vec3 energy)
{
    energy = vec3(1.0f);

    RayInfo ray;
    UpdateDirection(-wi, ray, mat.ax, mat.ay);
    UpdateHeight(1.0f, ray);

    // Random Walk
    int currentBounce = 0;
    while (true)
    {
        float u = Rnd(seed);
        UpdateHeight(SampleHeight(ray, u), ray);

        // leave the microsurface?
        if (ray.h == FLT_MAX)
            break;
        else
            currentBounce++;

        vec3 weight;
        UpdateDirection(SamplePhaseFunctionConductor(seed, -ray.dir, mat, weight), ray, mat.ax, mat.ay);
        energy = energy * weight;
        UpdateHeight(ray.h, ray);

        // if NaN (should not happen, just in case)
        if ((ray.h != ray.h) || (ray.dir.x != ray.dir.x))
        {
            energy = vec3(0.0f);
            return vec3(0, 0, 1);
        }

        if (currentBounce > maxBounces)
        {
            energy = vec3(0.0f);
            return vec3(0, 0, 1);
        }
    }

    return ray.dir;
}

vec3 GgxSampling(float alphaRoughness, float r1, float r2)
{
    float alphaSqr = max(alphaRoughness * alphaRoughness, 1e-07);

    float phi = 2.0 * M_PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (alphaSqr - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface)
{
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);

    vec3 energy;
    vec3 outDir = SampleConductorDirection(seed, V, mat, 10, energy);

    if (energy == vec3(0.0f)) // absorbed
        return false;

    //vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
    //outDir = reflect(-V, H);

    if (outDir.z < 0.0f)
        return false;

    data.PDF = 1.0f;
    data.BSDF = energy;

    outDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, outDir);
    data.RayDir = outDir;

    return true;
}

#endif