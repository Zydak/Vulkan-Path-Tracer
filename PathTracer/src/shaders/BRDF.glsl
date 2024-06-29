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
    // alphaRoughness    = roughness * roughness;
    float alphaSqr = max(alphaRoughness * alphaRoughness, 1e-07);

    float NdotHSqr = NdotH * NdotH;
    float denom = NdotHSqr * (alphaSqr - 1.0) + 1.0;

    return alphaSqr / (M_PI * denom * denom);
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

vec3 BrdfSpecular(float alphaRoughness, vec3 f0, vec3 f90, float NdotH, float NdotL, float NdotV, float LdotH)
{
    float D = DistributionGGX(NdotH, alphaRoughness);
    vec3 F = mix(f0, f90, SchlickWeight(LdotH));
    float G = SmithGGX(NdotL, NdotV, alphaRoughness);

    return (D * F * G);// / (4.0f * NdotL * NdotV); // denominator is already baked into SmithGGX function
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

    data.BRDF = vec3(0.0f);
    data.PDF = 0.0f;

    // Diffuse
    {
        data.BRDF += BrdfLambertian(mat.Color.xyz, 0.0f);
    
        // Cosine Weighted Distribution
        data.PDF += NdotL * M_1_OVER_PI;
    }

    // Specular
    {
        vec3 tint = mat.Color.xyz / max(GetLuminance(mat.Color.xyz), 0.001f);
        vec3 f0 = mix(vec3(mat.SpecularStrength) * mix(vec3(1.0f), tint, mat.SpecularTint), mat.Color.xyz, mat.Metallic);
        vec3 f90 = vec3(1.0F);
        float a = mat.Roughness * mat.Roughness;

        data.BRDF += BrdfSpecular(a, f0, f90, NdotH, NdotL, NdotV, LdotH);

        data.PDF += DistributionGGX(NdotH, a) /* * NdotH */ / (4.0F * LdotH); // (4.0F * LdotH) is a jacobian determinant according to some discord guy
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

void TintColors(Material mat, float eta, out float F0, out vec3 Cspec0)
{
    float lum = CalculateLuminance(mat.Color.xyz);
    vec3 ctint = lum > 0.0 ? mat.Color.xyz / lum : vec3(1.0);

    F0 = (1.0 - eta) / (1.0 + eta);
    F0 *= F0;

    Cspec0 = F0 * mix(vec3(1.0f), ctint, 1.0f);
    // Cspec0 = vec3(F0);
}

void SampleBRDF(inout BRDFSampleData data, in Material mat, in Surface surface)
{
    // Random numbers
    float r1 = data.Random.x;
    float r2 = data.Random.y;
    float r3 = data.Random.z;
    float r4 = data.Random.a;

    vec3 Cspec0;
    float F0;
    TintColors(mat, mat.eta, F0, Cspec0);
    float schlickWeight = SchlickWeight(WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View).z);

    // TODO: Recheck this
    // Lobe probabilities/weights
    float diffuseWeight = (1.0 - mat.Metallic) * (1.0 - mat.SpecTrans) * GetLuminance(mat.Color.xyz);
    float dielectricWeight = (1.0 - mat.Metallic) * (1.0 - mat.SpecTrans) * GetLuminance(mix(Cspec0, vec3(1.0), schlickWeight));
    float metalWeight = mat.Metallic * GetLuminance(mix(mat.Color.xyz, vec3(1.0), schlickWeight));

    float TotalWeight = (diffuseWeight + dielectricWeight + metalWeight);
    diffuseWeight /= TotalWeight;
    dielectricWeight /= TotalWeight;
    metalWeight /= TotalWeight;

    // CDF of the sampling probabilities/weights
    float diffuseCDF = diffuseWeight;
    float metallicCDF = diffuseCDF + metalWeight + dielectricWeight;

    vec3 reflectVector;
    vec3 brdf;
    float pdf;
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
        vec3 halfVector = GgxSampling(mat.Roughness * mat.Roughness, r3, r4);  // Glossy
        halfVector = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, halfVector);
    
        reflectVector = normalize(reflect(-data.View, halfVector));
    }

    BRDFEvaluateData evalData;
    evalData.Light = reflectVector;
    evalData.View = data.View;

    EvaluateBRDF(evalData, mat, surface);

    data.RayDir = reflectVector;
    data.PDF = evalData.PDF;
    data.BRDF = evalData.BRDF;
}

#endif