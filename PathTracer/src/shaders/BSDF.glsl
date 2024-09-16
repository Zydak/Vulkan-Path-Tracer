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
    float sinThetaTSq = (1.0f - (cosThetaI * cosThetaI)) * (eta * eta);

    // Total internal reflection
    if (sinThetaTSq >= 1.0)
        return 1.0;

    float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0001f));

    float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float rs = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface, in HitData hitData)
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

    bool hitFromTheInside = mat.eta > 1.0f;
    
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

        if (Rnd(seed) < F)
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

            if (hitFromTheInside)
            {
                payload.InMedium = true;
                payload.MediumID = gl_InstanceCustomIndexEXT;
            }
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
            }

            if (data.RayDir.z > 0.0f)
                return false;

            data.BSDF = vec3(mat.Color.xyz);

            reflection = false;

            if (!hitFromTheInside)
            {
                payload.InMedium = true;
                payload.MediumID = gl_InstanceCustomIndexEXT;
            }
        }

        data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
    }

    if (reflection && dot(surface.Normal, data.RayDir) < 0.0f)
        return false;

    return true;
}

#endif