#ifndef BSDFFile
#define BSDFFile

#include "raycommon.glsl"
#include "Sampling.glsl"

struct BSDFSampleData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  RayDir;       // [out] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BSDF;         // [out] BSDF
};

vec3 Fresnel(float LdotH, vec3 cspec)
{
    return cspec + (1.0f - cspec) * pow(1.0f - LdotH, 5.0f);
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface)
{
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);
    vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
    
    data.RayDir = reflect(-V, H);
    
    if (data.RayDir.z < 0.0f)
        return false;
    
    float NdotH = max(H.z, 0.0f);
    float NdotV = max(V.z, 0.0f);
    float NdotL = max(data.RayDir.z, 0.0f);
    float VdotH = max(dot(V, H), 0.0f);
    float LdotH = max(dot(data.RayDir, H), 0.0f);
    
    float D = GGXDistributionAnisotropic(H, mat.ax, mat.ay);
    vec3 F = Fresnel(LdotH, mat.Color.xyz);

    float GV = GGXSmithAnisotropic(V, mat.ax, mat.ay);
    float GL = GGXSmithAnisotropic(data.RayDir, mat.ax, mat.ay);
    float G = GV * GL;

    // BRDF = D * F * GV * GL / (4.0f * NdotV)
    // 
    // PDF is VNDF / jacobian of reflect()
    // PDF = (GV * VdotH * D / NdotV) / (4.0f * VdotH)
    //
    // F = BRDF / PDF
    //
    // If we expand it we get
    // 
    //      D * F * GV * GL * 4.0f * NdotV * VdotH
    //  F = --------------------------------------
    //      4.0f * VdotH * NdotV * GV * D
    //
    // almost everything cancels out and we're only left with F * GL. Noice.

    data.BSDF = F * GL;
    data.PDF = 1.0f;

    float energyCompensation = texture(uEnergyLookupTexture, vec2(V.z, mat.Roughness)).r;
    data.BSDF /= vec3(energyCompensation);
    
    data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);

    return true;
}

#endif