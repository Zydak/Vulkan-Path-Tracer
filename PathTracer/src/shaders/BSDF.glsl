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

vec3 GgxSampling(float alphaRoughness, float r1, float r2)
{
    float alphaSqr = max(alphaRoughness * alphaRoughness, 1e-07);

    float phi = 2.0 * M_PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (alphaSqr - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float SchlickWeight(float NdotV)
{
    float m = clamp(1.0 - NdotV, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

float DielectricFresnel(float cosThetaI, float eta)
{
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

    if (sinThetaTSq > 1.0)
        return 1.0;

    float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

    float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

vec3 customReflect(vec3 I, vec3 N) {
    float NdotI = dot(N, I);
    return I - 2.0 * NdotI * N;
}

vec3 SampleGGXVNDF(vec3 V, float ax, float ay, float r1, float r2)
{
    vec3 Vh = normalize(vec3(ax * V.x, ay * V.y, V.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
    vec3 T2 = cross(Vh, T1);

    float r = sqrt(r1);
    float phi = 2.0 * M_PI * r2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    return normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface)
{
    // Dot products
    float NdotV = abs(dot(surface.Normal, data.View));

    // Lobe probabilities
    float schlick = SchlickWeight(NdotV);

    float diffuseProb = (1.0f - mat.Metallic) * (1.0f - mat.SpecTrans);
    float dielectricProb = ((mat.SpecularStrength * 0.16) + schlick) * (1.0f - mat.Metallic);
    float metallicProb = mat.Metallic;
    float glassProb = (1.0f - mat.Metallic) * mat.SpecTrans;

    float totalProbInv = 1.0f / (diffuseProb + dielectricProb + metallicProb + glassProb);

    diffuseProb *= totalProbInv;
    dielectricProb *= totalProbInv;
    metallicProb *= totalProbInv;
    glassProb *= totalProbInv;

    float diffuseCDF = diffuseProb;
    float dielectricCDF = diffuseCDF + dielectricProb;
    float metallicCDF = dielectricCDF + metallicProb;
    float glassCDF = metallicCDF + glassProb;

    float random = Rnd(seed);

    data.PDF = 1.0f; // maybe some day
    if (random < diffuseCDF)
    {
        // Diffuse
        data.RayDir = CosineSamplingHemisphere(seed, surface.Normal);
    
        data.BSDF = mat.Color.xyz;
    }
    else if (random < dielectricCDF)
    {
        // Dielectric
        vec3 V = normalize(data.View);
        vec3 N = normalize(surface.Normal);
        vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
        H = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, H);

        data.RayDir = reflect(-V, H);

        if (dot(surface.Normal, data.RayDir) < 0.0f)
        {
            return false;
        }
    
        data.BSDF = mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);
    }
    else if (random < metallicCDF)
    {
        // Metal
        vec3 V = normalize(data.View);
        vec3 N = normalize(surface.Normal);
        vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
        H = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, H);
    
        data.RayDir = reflect(-V, H);

        if (dot(surface.Normal, data.RayDir) < 0.0f)
        {
            return false;
        }

        data.BSDF = mat.Color.xyz;
    }
    else if (random < glassCDF)
    {
        // Glass
        vec3 H = GgxSampling(mat.Roughness * mat.Roughness, Rnd(seed), Rnd(seed));
        H = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, H);
    
        float FresnelWeight = DielectricFresnel(abs(dot(data.View, H)), mat.eta);
    
        if (FresnelWeight > Rnd(seed))
        {
            data.RayDir = reflect(-data.View, H);
            data.BSDF = vec3(1.0f);

            if (dot(data.RayDir, surface.Normal) < 0.0f) // refraction
                data.BSDF = mat.Color.xyz;
        }
        else
        {
            data.RayDir = normalize(refract(-data.View, H, mat.eta));
    
            data.BSDF = mat.Color.xyz;
        }
    }

    return true;
}

#endif