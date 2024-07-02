#ifndef BRDFFile
#define BRDFFile

#include "raycommon.glsl"

struct BRDFEvaluateData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  Light;        // [in] Toward the sampled light / Outgoing ray dir
    vec3  BRDF;         // [out] BRDF
    float PDF;          // [out] PDF
};

struct BRDFSampleData
{
    vec3  View;         // [in] Toward the incoming ray
    vec4  Random;       // [in] 4 random [0..1]
    vec3  RayDir;       // [out] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BRDF;         // [out] BRDF
};

vec3 SampleHemi(in vec3 rand, in vec3 normal)
{
    float r1 = rand.x * 2.0f - 1.0f;
    float r2 = rand.y * 2.0f - 1.0f;
    float r3 = rand.z * 2.0f - 1.0f;

    vec3 direction = normalize(vec3(r1, r2, r3));
    if (dot(direction, normal) <= 0.0)
        direction = -direction;

    return direction;
}

float DistributionGGX(float NdotH, float alphaRoughness)
{
    float a2 = max(alphaRoughness * alphaRoughness, 0.0001f);
    float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (M_PI * f * f);
}

float SmithGGX(float NdotL, float NdotV, float alphaRoughness)
{
    float alphaRoughnessSq = max(alphaRoughness * alphaRoughness, 1e-07);

    // This could be approximated https://google.github.io/filament/Filament.html
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0F - alphaRoughnessSq) + alphaRoughnessSq);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0F - alphaRoughnessSq) + alphaRoughnessSq);

    return 0.5 / (ggxV + ggxL);
}

float SchlickWeight(float LdotH)
{
    float m = clamp(1.0 - LdotH, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

vec3 BrdfLambertian(vec3 diffuseColor, float metallic)
{
    return (1.0F - metallic) * (diffuseColor / M_PI);
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

float SmithG(float NDotV, float alphaG)
{
    float a = alphaG * alphaG;
    float b = NDotV * NDotV;
    return (2.0 * NDotV) / (NDotV + sqrt(a + b - a * b));
}

vec3 EvalDiffuse(Material mat, float NdotL, out float pdf)
{
    pdf = NdotL * M_1_OVER_PI;
    return (1.0F - mat.Metallic) * (mat.Color.xyz / M_PI);
}

vec3 EvalSpecular(Material mat, vec3 Cspec0, float NdotH, float NdotV, float NdotL, float LdotH, out float pdf)
{
    float a = mat.Roughness * mat.Roughness;

    float D = DistributionGGX(NdotH, a);
    vec3 F = mix(Cspec0, vec3(1.0F), SchlickWeight(LdotH));
    float G = SmithGGX(NdotL, NdotV, a);

    pdf = DistributionGGX(NdotH, a) * NdotH / (4.0F * LdotH);

    return vec3(D * F * G);
}

void EvaluateBRDF(inout BRDFEvaluateData data, in Material mat, in Surface surface)
{
    // Compute half vector
    vec3 halfVector = normalize(data.View + data.Light);

    // Compute angles between vectors
    float NdotV = abs(dot(surface.Normal, data.View));
    float NdotL = abs(dot(surface.Normal, data.Light));
    float VdotH = abs(dot(data.View, halfVector));
    float NdotH = abs(dot(surface.Normal, halfVector));
    float LdotH = abs(dot(data.Light, halfVector));

    vec3 L = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.Light);
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);
    vec3 H;
    if (L.z > 0.0)
        H = normalize(L + V);
    else
        H = normalize(L + V * mat.eta);

    if (H.z < 0.0)
        H = -H;
    float VdotHTangent = abs(dot(V, H));

    bool reflect = dot(surface.Normal, data.Light) > 0.0f;

    float lum = GetLuminance(mat.Color.xyz);
    vec3 ctint = lum > 0.0 ? mat.Color.xyz / lum : vec3(1.0);

    vec3 Cspec0 = mix(mix(vec3(mat.SpecularStrength), ctint, mat.SpecularTint), mat.Color.xyz, mat.Metallic);
    //vec3 Cspec0 = 0.16f * mat.SpecularStrength * mat.SpecularStrength * (1.0f - mat.Metallic) + mat.Color.xyz * mat.Metallic;

    float schlickWeight = SchlickWeight(abs(dot(data.View, surface.Normal)));

    data.BRDF = vec3(0.0f);
    data.PDF = 0.0f;

    // Diffuse
    {
        float pdf;
        data.BRDF += EvalDiffuse(mat, NdotL, pdf);
        data.PDF += pdf;
    }
    
    // Specular
    {
        float pdf;
        data.BRDF += EvalSpecular(mat, Cspec0, NdotH, NdotV, NdotL, LdotH, pdf);
        data.PDF += pdf;
    }

    data.BRDF *= NdotL; // Cosine term
}

vec3 GgxSampling(float alphaRoughness, float r1, float r2)
{
    float alphaSqr = max(alphaRoughness * alphaRoughness, 1e-07);

    float phi = 2.0 * M_PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (alphaSqr - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

vec3 CosineSampleHemisphere(float r1, float r2)
{
    float r = sqrt(r1);
    float phi = M_TWO_PI * r2;
    vec3  dir;
    dir.x = r * cos(phi);
    dir.y = r * sin(phi);
    dir.z = sqrt(max(0.0, 1.0 - dir.x * dir.x - dir.y * dir.y));
    return dir;
}

void SampleBRDF(inout BRDFSampleData data, in Material mat, in Surface surface)
{
    // Random numbers
    float r1 = data.Random.x;
    float r2 = data.Random.y;
    float r3 = data.Random.z;
    float r4 = data.Random.a;

    // TODO: Recheck this
    // Lobe probabilities/weights

    float diffuseWeight = (1.0 - mat.Metallic);
    float dielectricWeight = (1.0 - mat.Metallic);
    float metalWeight = mat.Metallic;

    float TotalWeightInv = 1.0f / (diffuseWeight + dielectricWeight + metalWeight);
    diffuseWeight *= TotalWeightInv;
    dielectricWeight *= TotalWeightInv;
    metalWeight *= TotalWeightInv;

    // CDF of the sampling probabilities/weights
    float diffuseCDF = diffuseWeight;
    float metallicCDF = diffuseCDF + metalWeight + dielectricWeight;

    vec3 reflectVector;
    if (r1 < diffuseCDF)
    {
        //reflectVector = SampleHemi(vec3(r2, r3, r4), surface.Normal);
        reflectVector = CosineSampleHemisphere(r3, r4);  // Diffuse
        reflectVector = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, reflectVector);
        if (dot(reflectVector, surface.Normal) <= 0.0f)
            reflectVector = -reflectVector;
    }
    else if (r1 < metallicCDF)
    {
        vec3 halfVector = GgxSampling(mat.Roughness * mat.Roughness, r3, r4);
        halfVector = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, halfVector);
    
        reflectVector = normalize(reflect(-data.View, halfVector));
    }

    data.RayDir = reflectVector;
    BRDFEvaluateData evalData;
    evalData.Light = reflectVector;
    evalData.View = data.View;
    
    EvaluateBRDF(evalData, mat, surface);
    
    data.RayDir = reflectVector;
    data.PDF = evalData.PDF;
    data.BRDF = evalData.BRDF;
}

#endif