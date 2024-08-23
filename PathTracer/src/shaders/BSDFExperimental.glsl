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

vec3 GgxSampling(float roughness, float r1, float r2)
{
    float alpha = max(roughness * roughness, 1e-07);
    float alphaSqr = alpha * alpha;

    float phi = 2.0 * M_PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (alphaSqr - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float D_GGX(float NdotH, float roughness)
{
    float r2 = roughness * roughness + 0.0001f;
    float denom = r2 * pow(NdotH, 4.0f);
    float NdotH2 = NdotH * NdotH;

    float exponent = (NdotH2 - 1.0f) / (r2 * NdotH2);
    return (1.0f / denom) * exp(exponent);
}

vec3 Fresnel(float LdotH, vec3 cspec)
{
    return cspec + (vec3(1.0f) - cspec) * pow(1.0f - LdotH, 5.0f);
}

float G_Shadowing(float NdotH, float NdotV, float NdotL, float HdotV)
{
    float term1 = (2.0 * NdotH * NdotV) / HdotV;
    float term2 = (2.0 * NdotH * NdotL) / HdotV;

    return min(1.0f, min(term1, term2));
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface)
{
    vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
    H = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, H);

    data.RayDir = reflect(-data.View, H);

    if ((dot(surface.Normal, data.RayDir) < 0.0f) || (dot(surface.NormalNoTex, data.RayDir) < 0.0f))
        return false;

    float NdotH = dot(surface.Normal, H);
    float LdotH = dot(data.RayDir, H);
    float NdotV = dot(surface.Normal, data.View);
    float NdotL = dot(surface.Normal, data.RayDir);
    float VdotH = dot(data.View, H);

    float D = D_GGX(NdotH, mat.Roughness);
    vec3 F = Fresnel(LdotH, mat.Color.xyz);
    float G = G_Shadowing(NdotH, NdotV, NdotL, VdotH);

    vec3 Fs = vec3(D * F * G) / (4.0f * NdotV * NdotL) * NdotL;

    data.PDF = (D * NdotH) / (4.0f * VdotH);
    data.BSDF = Fs;

    return true;
}

#endif