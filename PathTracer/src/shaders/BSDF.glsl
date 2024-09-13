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

struct BSDFEvaluateData
{
    vec3  View;         // [in] Toward the incoming ray
    vec3  RayDir;       // [in] Reflect Dir
    float PDF;          // [out] PDF
    vec3  BSDF;         // [out] BSDF
};

float SchlickWeight(float VdotH)
{
    float m = clamp(1.0 - VdotH, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

float DielectricFresnel(float VdotH, float eta)
{
    float cosThetaI = VdotH;
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

    // Total internal reflection
    if (sinThetaTSq > 1.0)
        return 1.0;

    float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

    float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface)
{
    vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);

    float F0 = (1.0f - mat.eta) / (1.0f + mat.eta);
    F0 *= F0;

    float diffuseProbability = (1.0f - mat.Metallic) * (1.0f - mat.Transparency);
    float metallicProbability = mat.Metallic;
    float dieletricProbability = (1.0f - mat.Metallic) * F0 * (1.0f - mat.Transparency);
    float glassProbability = (1.0f - mat.Metallic) * mat.Transparency;

    float inverseTotalProbability = 1.0f / (diffuseProbability + metallicProbability + dieletricProbability + glassProbability);

    diffuseProbability *= inverseTotalProbability;
    metallicProbability *= inverseTotalProbability;
    dieletricProbability *= inverseTotalProbability;
    glassProbability *= inverseTotalProbability;

    float diffuseCDF = diffuseProbability;
    float metallicCDF = diffuseCDF + metallicProbability;
    float dielectricCDF = metallicCDF + dieletricProbability;
    float glassCDF = dielectricCDF + glassProbability;

    float r1 = Rnd(seed);
    
    data.PDF = 1.0f;
    bool reflection = true;
    if (r1 < diffuseCDF)
    {
        // Diffuse
    
        data.RayDir = CosineSamplingHemisphere(seed, surface.Normal);

        data.BSDF = mat.Color.xyz;
    }
    else if (r1 < metallicCDF)
    {
        // Metallic
    
        vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
        data.RayDir = normalize(reflect(-V, H));

        vec3 F = mix(mat.Color.xyz, vec3(1.0f), SchlickWeight(dot(V, H)));

        for (int i = 0; i < 500; i++)
        {
            if (data.RayDir.z > 0.0f)
                break;

            vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
            data.RayDir = normalize(reflect(-V, H));

            F *= mix(mat.Color.xyz, vec3(1.0f), SchlickWeight(dot(V, H)));
        }

        if (data.RayDir.z < 0.0f)
            return false;

        data.BSDF = F;
    
        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }
    else if (r1 < dielectricCDF)
    {
        // Dielectric

        vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
        data.RayDir = normalize(reflect(-V, H));

        vec3 Color = mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);

        for (int i = 0; i < 500; i++)
        {
            if (data.RayDir.z > 0.0f)
                break;

            vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
            data.RayDir = normalize(reflect(-V, H));

            Color *= mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);
        }

        if (data.RayDir.z < 0.0f)
            return false;

        data.BSDF = Color;

        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }
    else if (r1 < glassCDF)
    {
        // Glass

        vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
        float F = DielectricFresnel(abs(dot(V, H)), mat.eta);

        float r2 = Rnd(seed);

        bool reflection = false;
        if (r2 < F)
        {
            // Reflect
            data.RayDir = normalize(reflect(-V, H));

            vec3 Color = mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);

            for (int i = 0; i < 500; i++)
            {
                if (data.RayDir.z > 0.0f)
                    break;

                vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
                data.RayDir = normalize(reflect(-V, H));

                Color *= mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);
            }

            if (data.RayDir.z < 0.0f)
                return false;

            data.BSDF = Color;
            reflection = true;
        }
        else
        {
            // Refract

            data.RayDir = normalize(refract(-V, H, mat.eta));

            vec3 Color = mat.Color.xyz;

            for (int i = 0; i < 500; i++)
            {
                if (data.RayDir.z < 0.0f)
                    break;

                vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
                data.RayDir = normalize(reflect(-V, H));

                Color *= mat.Color.xyz;
            }

            if (data.RayDir.z > 0.0f)
                return false;

            data.BSDF = Color;

            reflection = false;
        }

        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }

    if (reflection && dot(surface.Normal, data.RayDir) < 0.0f)
        return false;

    return true;
}

#endif