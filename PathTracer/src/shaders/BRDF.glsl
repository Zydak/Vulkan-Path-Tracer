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
    vec3  RayDir;       // [out] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BRDF;         // [out] BRDF
};

vec3 BrdfLambertian(vec3 diffuseColor, float metallic)
{
    return (1.0F - metallic) * (diffuseColor / M_PI);
}

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

    // Lobe probabilities
    float schlick = SchlickWeight(NdotV);
    float specularAngle = mat.SpecularAngle / 90.0f;

    float diffuseWeight = (1.0f - mat.Metallic) * (1.0f - mat.SpecTrans);
    float dielectricWeight = mat.SpecularStrength * (schlick + (specularAngle));
    float metallicWeight = mat.Metallic;
    float glassWeight = (1.0f - mat.Metallic) * mat.SpecTrans;

    float totalWeightInv = 1.0f / (diffuseWeight + dielectricWeight + metallicWeight + glassWeight);

    float diffuseProb = diffuseWeight * totalWeightInv;
    float dielectricProb = dielectricWeight * totalWeightInv;
    float metallicProb = metallicWeight * totalWeightInv;
    float glassProb = glassWeight * totalWeightInv;

    bool reflect = dot(surface.Normal, data.Light) > 0.0f;

    // Diffuse
    {
        data.BRDF += mat.Color.xyz * diffuseProb;
        data.PDF += diffuseProb;
    }

    // Dielectric
    {
        data.BRDF += mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint) * dielectricProb;
        data.PDF += dielectricProb;
    }

    // Metallic
    {
        data.BRDF += mat.Color.xyz * metallicProb;
        data.PDF += metallicProb;
    }

    // Glass
    {
        if (reflect)
        {
            data.BRDF += vec3(1.0f) * glassProb;
            data.PDF += glassProb;
        }
        else
        {
            data.BRDF += mat.Color.xyz * glassProb;
            data.PDF += glassProb;
        }
    }

    //data.PDF = 1.0f;
}

void SampleBRDF(inout uint seed, inout BRDFSampleData data, in Material mat, in Surface surface)
{
    // Dot products
    float NdotV = abs(dot(surface.Normal, data.View));

    // Lobe probabilities
    float schlick = DielectricFresnel(NdotV, mat.eta);
    float specularAngle = mat.SpecularAngle / 90.0f;

    float diffuseProb = (1.0f - mat.Metallic) * (1.0f - mat.SpecTrans);
    float dielectricProb = mat.SpecularStrength * (schlick + (specularAngle));
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

    data.PDF = 1.0f;
    if (random < diffuseCDF)
    {
        // Lambertian
        data.RayDir = UniformSamplingHemisphere(seed, surface.Normal);
        data.RayDir = CosineSamplingHemisphere(seed, surface.Normal);
    
        data.BRDF = mat.Color.xyz;
    }
    else if (random < dielectricCDF)
    {
        // Dielectric
        vec3 halfVector = GgxSampling(mat.Roughness * mat.Roughness, Rnd(seed), Rnd(seed));
        halfVector = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, halfVector);
        data.RayDir = reflect(-data.View, halfVector);
    
        data.BRDF = mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);
    }
    else if (random < metallicCDF)
    {
        // Metal
        vec3 halfVector = GgxSampling(mat.Roughness * mat.Roughness, Rnd(seed), Rnd(seed));
        halfVector = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, halfVector);
        data.RayDir = reflect(-data.View, halfVector);
    
        data.BRDF = mat.Color.xyz;
    }
    else if (random < glassCDF)
    {
        // Glass
        vec3 halfVector = GgxSampling(mat.Roughness * mat.Roughness, Rnd(seed), Rnd(seed));
        halfVector = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, halfVector);

        float FresnelWeight = DielectricFresnel(abs(dot(data.View, halfVector)), mat.eta);

        if (FresnelWeight > Rnd(seed))
        {
            data.RayDir = reflect(-data.View, halfVector);

            data.BRDF = vec3(1.0f);
        }
        else
        {
            data.RayDir = normalize(refract(-data.View, halfVector, mat.eta));

            data.BRDF = mat.Color.xyz;
        }
    }

    // Still not working
    //BRDFEvaluateData evalData;
    //evalData.View = data.View;
    //evalData.Light = data.RayDir;
    //
    //EvaluateBRDF(evalData, mat, surface);
    //
    //data.BRDF = evalData.BRDF;
    //data.PDF = evalData.PDF;
}

#endif