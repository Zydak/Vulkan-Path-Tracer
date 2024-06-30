#ifndef PBR
#define PBR

// Based on https://github.com/knightcrawler25/GLSL-PathTracer

#include "raycommon.glsl"

#define EVENT_TYPE_END 100
#define EVENT_TYPE_OK 1

struct BsdfEvaluateData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  Light;        // [in] Toward the sampled light
    vec3  Bsdf;         // [out] BSDF
    float Pdf;          // [out] PDF
};

struct BsdfSampleData
{
    vec3  View;         // [in] Toward the incoming ray
    vec4  Random;       // [in] 4 random [0..1]
    vec3  RayDir;       // [out] Reflect Dir
    float Pdf;          // [out] PDF
    vec3  Bsdf;         // [out] BSDF
    int   EventType;    // [out] END or OK
};

vec3 BrdfLambertian(vec3 diffuseColor, float metallic)
{
    return (1.0F - metallic) * (diffuseColor / M_PI);
}

float DistributionGGX(float NdotH, float alphaRoughness)
{
    // alphaRoughness    = roughness * roughness;
    float alphaSqr = max(alphaRoughness * alphaRoughness, 1e-07);

    float NdotHSqr = NdotH * NdotH;
    float denom = NdotHSqr * (alphaSqr - 1.0) + 1.0;

    return alphaSqr / (M_PI * denom * denom);
}

vec3 FresnelSchlick(vec3 f0, vec3 f90, float VdotH)
{
    return f0 + (f90 - f0) * pow(clamp(vec3(1.0F) - VdotH, vec3(0.0F), vec3(1.0F)), vec3(5.0F));
}

float SmithJointGGX(float NdotL, float NdotV, float alphaRoughness)
{
    float alphaRoughnessSq = max(alphaRoughness * alphaRoughness, 1e-07);

    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0F - alphaRoughnessSq) + alphaRoughnessSq);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0F - alphaRoughnessSq) + alphaRoughnessSq);

    float ggx = ggxV + ggxL;
    if (ggx > 0.0F)
    {
        return 0.5F / ggx;
    }
    return 0.0F;
}

vec3 BrdfSpecularGGX(vec3 f0, vec3 f90, float alphaRoughness, float VdotH, float NdotL, float NdotV, float nDotH)
{
    vec3  f = FresnelSchlick(f0, f90, VdotH);
    float vis = SmithJointGGX(NdotL, NdotV, alphaRoughness);  // Vis = G / (4 * NdotL * NdotV)
    float d = DistributionGGX(nDotH, alphaRoughness);

    return f * vis * d;
}

float ClampedDot(vec3 x, vec3 y)
{
    return clamp(dot(x, y), 0.0F, 1.0F);
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

float GTR2Anisotropic(float NDotH, float HDotX, float HDotY, float ax, float ay)
{
    float a = HDotX / ax;
    float b = HDotY / ay;
    float c = a * a + b * b + NDotH * NDotH;
    return 1.0 / (M_PI * ax * ay * c * c);
}

float SmithGAnisotropic(float NDotV, float VDotX, float VDotY, float ax, float ay)
{
    float a = VDotX * ax;
    float b = VDotY * ay;
    float c = NDotV;
    return (2.0 * NDotV) / (NDotV + sqrt(a * a + b * b + c * c));
}

vec3 EvaluateMicrofacetReflection(Material mat, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
    pdf = 0.0;
    if (L.z <= 0.0)
        return vec3(0.0);

    float D = GTR2Anisotropic(H.z, H.x, H.y, mat.ax, mat.ay);
    float G1 = SmithGAnisotropic(abs(V.z), V.x, V.y, mat.ax, mat.ay);
    float G2 = G1 * SmithGAnisotropic(abs(L.z), L.x, L.y, mat.ax, mat.ay);

    pdf = G1 * D / (4.0 * V.z);
    return F * D * G2 / (4.0 * L.z * V.z);
}

vec3 EvalMicrofacetRefraction(Material mat, float eta, vec3 V, vec3 L, vec3 H, vec3 F, out float pdf)
{
    pdf = 0.0;
    if (L.z >= 0.0)
        return vec3(0.0);

    float LDotH = dot(L, H);
    float VDotH = dot(V, H);

    float D = GTR2Anisotropic(H.z, H.x, H.y, mat.ax, mat.ay);
    float G1 = SmithGAnisotropic(abs(V.z), V.x, V.y, mat.ax, mat.ay);
    float G2 = G1 * SmithGAnisotropic(abs(L.z), L.x, L.y, mat.ax, mat.ay);
    float denom = LDotH + VDotH * eta;
    denom *= denom;
    float eta2 = eta * eta;
    float jacobian = abs(LDotH) / denom;

    pdf = G1 * max(0.0, VDotH) * D * jacobian / V.z;
    return pow(mat.Color.xyz, vec3(0.5)) * (1.0 - F) * D * G2 * abs(VDotH) * jacobian * eta2 / abs(L.z * V.z);
}

float SchlickWeight(float u)
{
    float m = clamp(1.0 - u, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
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

void BsdfEvaluate(inout BsdfEvaluateData data, in Surface surface, in Material mat)
{
    // Initialization
    const vec3 surfaceNormal = surface.Normal;
    const vec3 tangent = surface.Tangent;
    const vec3 bitangent = surface.Bitangent;
    vec3 albedo = mat.Color.rgb;
    float metallic = mat.Metallic;
    float roughness = mat.Roughness;
    vec3 f0 = mix(vec3(0.04f), mat.Color.xyz, metallic);
    vec3 f90 = vec3(1.0F);

    // Specular roughness
    float alpha = roughness * roughness;

    // Compute half vector
    vec3 halfVector = normalize(data.View + data.Light);

    // Compute various "angles" between vectors
    float NdotV = abs(dot(surfaceNormal, data.View));
    float NdotL = abs(dot(surfaceNormal, data.Light));
    float VdotH = abs(dot(data.View, halfVector));
    float NdotH = abs(dot(surfaceNormal, halfVector));
    float LdotH = abs(dot(data.Light, halfVector));

    vec3 L = WorldToTangent(surface.Tangent, surface.Bitangent, surface.GeoNormal, data.Light);
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.GeoNormal, data.View);
    bool reflect = L.z * V.z > 0;

    vec3 H;
    if (L.z > 0.0)
        H = normalize(L + V);
    else
        H = normalize(L + V * mat.eta);

    if (H.z < 0.0)
        H = -H;

    vec3 Cspec0;
    float F0;
    TintColors(mat, mat.eta, F0, Cspec0);

#ifndef USE_GLOSSY
    mat.Metallic = 0.0f;
#endif
#ifndef USE_GLASS
    mat.SpecTrans = 0.0f;
#endif

    // Model weights
    float dielectricWeight = (1.0 - mat.Metallic) * (1.0 - mat.SpecTrans);
    float metalWeight = mat.Metallic;
    float glassWeight = (1.0 - mat.Metallic) * mat.SpecTrans;

    // Lobe probabilities
    float schlickWeight = SchlickWeight(V.z);

    float diffProbability = dielectricWeight * CalculateLuminance(mat.Color.xyz);
    float dielectricProbability = dielectricWeight * CalculateLuminance(mix(Cspec0, vec3(1.0), schlickWeight));
    float metalProbability = metalWeight * CalculateLuminance(mix(mat.Color.xyz, vec3(1.0), schlickWeight));
    float glassProbability = glassWeight;
    float clearCoatProbability = 0.25 * mat.Clearcoat;


#ifndef USE_GLASS
    glassProbability = 0.0f;
#endif
#ifndef USE_GLOSSY
    dielectricProbability = 0.0f;
    metalProbability = 0.0f;
#endif
#ifndef USE_CLEARCOAT
    clearCoatProbability = 0.0f;
#endif

    // Normalize probabilities
    float TotalWeight = (diffProbability + dielectricProbability + metalProbability + glassProbability + clearCoatProbability);
    diffProbability /= TotalWeight;
    dielectricProbability /= TotalWeight;
    metalProbability /= TotalWeight;
    glassProbability /= TotalWeight;
    clearCoatProbability /= TotalWeight;

    float VdotHTangent = abs(dot(V, H));

    float diffusePDF = 0.0f;
    float specularPDF = 0.0f;
    data.Bsdf = vec3(0.0f);
    data.Pdf = 0.0f;
    float tmpPdf = 0.0;
    if (diffProbability > 0.0 && reflect)
    {
        // Contribution
        data.Bsdf += BrdfLambertian(albedo, metallic);

        // Calculate PDF (probability density function)
        data.Pdf += (NdotL * M_1_OVER_PI);
    }
#ifdef USE_DIELECTRIC
    if (dielectricProbability > 0.0 && reflect)
    {
        // Normalize for interpolating based on Cspec0
        float F = (DielectricFresnel(VdotHTangent, 1.0 / mat.Ior) - F0) / (1.0 - F0);

        data.Bsdf += EvaluateMicrofacetReflection(mat, V, L, H, mix(Cspec0, vec3(1.0), F), tmpPdf) * dielectricWeight;
        data.Pdf += tmpPdf * dielectricProbability;
    }
#endif
#ifdef USE_GLOSSY
    if (metalProbability > 0.0 && reflect)
    {
        // Contribution
        data.Bsdf += BrdfSpecularGGX(f0, f90, alpha, VdotH, NdotL, NdotV, NdotH);

        // Calculate PDF (probability density function)
        data.Pdf += DistributionGGX(NdotH, alpha) * NdotH / (4.0F * LdotH);
    }
#endif
#ifdef USE_GLASS
    if (glassProbability > 0.0)
    {
        float F = DielectricFresnel(VdotHTangent, mat.eta);

        if (reflect)
        {
            data.Bsdf += EvaluateMicrofacetReflection(mat, V, L, H, vec3(F), tmpPdf) * glassWeight;
            data.Pdf += tmpPdf * glassProbability * F;
        }
        else
        {
            data.Bsdf += EvalMicrofacetRefraction(mat, mat.eta, V, L, H, vec3(F), tmpPdf) * glassWeight;
            data.Pdf += tmpPdf * glassProbability * (1.0 - F);
        }
    }
#endif

    data.Bsdf *= NdotL;
}

vec3 GTR1(float rgh, float r1, float r2)
{
    float a = max(0.001, rgh);
    float a2 = a * a;

    float phi = r1 * M_TWO_PI;

    float cosTheta = sqrt((1.0 - pow(a2, 1.0 - r2)) / (1.0 - a2));
    float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);

    return vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

void BsdfSample(inout BsdfSampleData data, in Surface surface, in Material mat)
{
    // Initialization
    vec3  surfaceNormal = surface.Normal;
    float roughness = mat.Roughness;
    float metallic = mat.Metallic;
    vec3 tangent = surface.Tangent;
    vec3 bitangent = surface.Bitangent;

    // Random numbers for importance sampling
    float r1 = data.Random.x;
    float r2 = data.Random.y;
    float r3 = data.Random.z;

    // Specular roughness
    float alpha = roughness * roughness;

    vec3 Cspec0;
    float F0;
    TintColors(mat, mat.eta, F0, Cspec0);

#ifndef USE_GLOSSY
    mat.Metallic = 0.0f;
#endif
#ifndef USE_GLASS
    mat.SpecTrans = 0.0f;
#endif

    float dielectricWeight = (1.0 - mat.Metallic) * (1.0 - mat.SpecTrans);
    float metalWeight = mat.Metallic;
    float glassWeight = (1.0 - mat.Metallic) * mat.SpecTrans;

    // Lobe probabilities
    float schlickWeight = SchlickWeight(WorldToTangent(surface.Tangent, surface.Bitangent, surfaceNormal, data.View).z);

    float diffProbability = dielectricWeight * CalculateLuminance(mat.Color.xyz);
    float dielectricProbability = dielectricWeight * CalculateLuminance(mix(Cspec0, vec3(1.0), schlickWeight));
    float metalProbability = metalWeight * CalculateLuminance(mix(mat.Color.xyz, vec3(1.0), schlickWeight));
    float glassProbability = glassWeight;
    float clearCoatProbability = 0.25 * mat.Clearcoat;

#ifndef USE_GLASS
    glassProbability = 0.0f;
#endif
#ifndef USE_GLOSSY
    dielectricProbability = 0.0f;
    metalProbability = 0.0f;
#endif
#ifndef USE_CLEARCOAT
    clearCoatProbability = 0.0f;
#endif

    // Normalize probabilities
    float TotalWeight = (diffProbability + dielectricProbability + metalProbability + glassProbability + clearCoatProbability);
    diffProbability /= TotalWeight;
    dielectricProbability /= TotalWeight;
    metalProbability /= TotalWeight;
    glassProbability /= TotalWeight;
    clearCoatProbability /= TotalWeight;

    // CDF of the sampling probabilities
    float diffuseCDF = diffProbability;
    float dielectricCDF = diffuseCDF + dielectricProbability;
    float metallicCDF = dielectricCDF + metalProbability;
    float glassCDF = metallicCDF + glassProbability;
    float clearCoatCDF = glassCDF + clearCoatProbability;

    vec3 halfVector;
    vec3 reflectVector;
    if (r3 < diffuseCDF)
    {
        reflectVector = CosineSampleHemisphere(r1, r2);  // Diffuse
        reflectVector = TangentToWorld(tangent, bitangent, surfaceNormal, reflectVector);
    }
#ifdef USE_GLOSSY
    else if (r3 < metallicCDF)
    {
        halfVector = GgxSampling(alpha, r1, r2);  // Glossy
        halfVector = TangentToWorld(tangent, bitangent, surfaceNormal, halfVector);

        // Compute the reflection direction from the sampled half vector and view direction
        reflectVector = reflect(-data.View, halfVector);
    }
#endif
#ifdef USE_GLASS
    else if (r3 < glassCDF) // Glass
    {
        halfVector = GgxSampling(mat.Roughness, r1, r2);
        halfVector = TangentToWorld(tangent, bitangent, surfaceNormal, halfVector);

        float F = DielectricFresnel(abs(dot(data.View, halfVector)), mat.eta);

        // Rescale random number for reuse
        r3 = (r3 - metallicCDF) / (glassCDF - metallicCDF);

        // Reflection
        if (r3 < F)
        {
            reflectVector = normalize(reflect(-data.View, halfVector));
        }
        else // refraction
        {
            reflectVector = normalize(refract(-data.View, halfVector, mat.eta));
        }
    }
#endif
#ifdef USE_CLEARCOAT
    else if (r3 < clearCoatCDF) // Clearcoat
    {
        halfVector = GTR1(mat.ClearcoatRoughness, r1, r2);

        if (halfVector.z < 0.0)
            halfVector = -halfVector;

        halfVector = TangentToWorld(tangent, bitangent, surfaceNormal, halfVector);

        reflectVector = normalize(reflect(-data.View, halfVector));
    }
#endif

    // Evaluate the refection coefficient with this new ray direction
    BsdfEvaluateData evalData;
    evalData.View = data.View;
    evalData.Light = reflectVector;
    BsdfEvaluate(evalData, surface, mat);

    // Return values
    data.Pdf = evalData.Pdf;
    data.Bsdf = evalData.Bsdf;
    data.EventType = EVENT_TYPE_OK;
    data.RayDir = reflectVector;

    if (data.Pdf <= 0.0)
        data.EventType = EVENT_TYPE_END;

    return;
}

#else
#endif