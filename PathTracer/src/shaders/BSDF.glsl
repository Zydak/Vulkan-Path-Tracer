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
    float sinThetaTSq = (1.0f - cosThetaI * cosThetaI) * eta * eta;

    if (sinThetaTSq > 1.0)
        return 1.0;

    float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

    float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
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
    
        // Multiple surface scattering, if ray is blocked by a microfacet and it goes below the surface
        // bounce it again instead of ending the path
        for (int i = 0; i < 10; i++)
        {
            vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
            H = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, H);

            data.RayDir = reflect(-data.View, H);

            if ((dot(surface.Normal, data.RayDir) > 0.0f) && (dot(surface.NormalNoTex, data.RayDir) > 0.0f))
            {
                break;
            }
        }

        // When using normal mapping the direction generated might be wrong sometimes and go under the surface
        // in that case just generate a direction based on the geomtry normal and ignore the texture normal
        // it's not ideal but still better than just discarding the sample
        if (dot(surface.NormalNoTex, data.RayDir) < 0.0f)
        {
            vec3 T;
            vec3 B;
            vec3 N = surface.NormalNoTex;

            CalculateTangents(N, T, B);

            for (int i = 0; i < 5; i++)
            {
                vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
                H = TangentToWorld(T, B, N, H);

                data.RayDir = reflect(-data.View, H);

                if ((dot(surface.Normal, data.RayDir) > 0.0f) && (dot(surface.NormalNoTex, data.RayDir) > 0.0f))
                {
                    break;
                }
            }
        }

        // If for some reason it's still wrong avoid the internal reflection and discard the sample
        if ((dot(surface.Normal, data.RayDir) < 0.0f) || (dot(surface.NormalNoTex, data.RayDir) < 0.0f))
            return false;

        data.BSDF = mix(vec3(1.0f), mat.Color.xyz, mat.SpecularTint);
    }
    else if (random < metallicCDF)
    {
        // Metal
    
        // Multiple surface scattering, if ray is blocked by a microfacet and it goes below the surface
        // bounce it again instead of ending the path
        for (int i = 0; i < 10; i++)
        {
            vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
            H = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, H);

            data.RayDir = reflect(-data.View, H);

            if ((dot(surface.Normal, data.RayDir) > 0.0f) && (dot(surface.NormalNoTex, data.RayDir) > 0.0f))
            {
                break;
            }
        }

        // When using normal mapping the direction generated might be wrong sometimes and go under the surface
        // in that case just generate a direction based on the geomtry normal and ignore the texture normal
        // it's not ideal but still better than just discarding the sample
        if (dot(surface.NormalNoTex, data.RayDir) < 0.0f)
        {
            vec3 T;
            vec3 B;
            vec3 N = surface.NormalNoTex;

            CalculateTangents(N, T, B);

            for (int i = 0; i < 5; i++)
            {
                vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
                H = TangentToWorld(T, B, N, H);

                data.RayDir = reflect(-data.View, H);

                if ((dot(surface.Normal, data.RayDir) > 0.0f) && (dot(surface.NormalNoTex, data.RayDir) > 0.0f))
                {
                    break;
                }
            }
        }

        // If for some reason it's still wrong avoid the internal reflection and discard the sample
        if ((dot(surface.Normal, data.RayDir) < 0.0f) || (dot(surface.NormalNoTex, data.RayDir) < 0.0f))
            return false;

        data.BSDF = mat.Color.xyz;
    }
    else if (random < glassCDF)
    {
        // Glass
        vec3 H = GgxSampling(mat.Roughness, Rnd(seed), Rnd(seed));
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