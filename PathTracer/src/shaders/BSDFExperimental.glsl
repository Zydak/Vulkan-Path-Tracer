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

float SchlickWeight(float VdotH)
{
	float m = clamp(1.0 - VdotH, 0.0, 1.0);
	float m2 = m * m;
	return m2 * m2 * m;
}

vec3 EvalDiffuse(in Material mat, vec3 L, out float pdf)
{
	// BRDF = Albedo / M_INV_PI * NdotL
	// PDF = NdotL / M_INV_PI
	// 
	// Fr = BRDF / PDF
	// 
	// If we expand it we get
	// 
	//      Albedo * NdotL * M_INV_PI
	// Fr = ---------------------
	//          M_INV_PI * NdotL
	//
	// So we're only left with Fr = Albedo.

	pdf = L.z * M_1_OVER_PI;
	vec3 bsdf = M_1_OVER_PI * mat.Color.xyz * L.z;

	//pdf = 1.0f;
	//vec3 bsdf = mat.Color.xyz;

	return bsdf;
}

vec3 EvalReflection(in Material mat, vec3 L, vec3 V, vec3 H, vec3 F, out float pdf)
{
	// BRDF = D * F * GV * GL / (4.0f * NdotV * NdotL) * NdotL
	// 
	// PDF is VNDF / jacobian of reflect()
	// PDF = (GV * VdotH * D / NdotV) / (4.0f * VdotH)
	//
	// Fr = BRDF / PDF
	//
	// If we expand it we get
	// 
	//      D * F * GV * GL * 4.0f * NdotV * NdotL * VdotH
	// Fr = ----------------------------------------------
	//          4.0f * NdotL * VdotH * NdotV * GV * D
	//
	// almost everything cancels out and we're only left with F * GL. Noice.

	float LdotH = max(0.0f, dot(L, H));
	float VdotH = max(0.0f, dot(V, H));

	float D = GGXDistributionAnisotropic(H, mat.ax, mat.ay);

	float GV = GGXSmithAnisotropic(V, mat.ax, mat.ay);
	float GL = GGXSmithAnisotropic(L, mat.ax, mat.ay);
	float G = GV * GL;

	//pdf = 1.0f;
	//vec3 bsdf = F * GL;

	pdf = (GV * VdotH * D / V.z) / (4.0f * VdotH);
	vec3 bsdf = D * F * GV * GL / (4.0f * V.z);

	return bsdf;
}

vec3 EvalDielectricRefraction(in Material mat, in Surface surface, vec3 L, vec3 V, vec3 H, float F, out float pdf)
{
	float VdotH = abs(dot(V, H));
	float LdotH = abs(dot(L, H));

	float D = GGXDistributionAnisotropic(H, mat.ax, mat.ay);
	float GV = GGXSmithAnisotropic(V, mat.ax, mat.ay);
	float GL = GGXSmithAnisotropic(L, mat.ax, mat.ay);
	float G = GV * GL;

	float denominator = (LdotH + mat.eta * VdotH);
	float denominator2 = denominator * denominator;
	float eta2 = mat.eta * mat.eta;

	float jacobian = (eta2 * LdotH) / denominator2;

	pdf = (GV * VdotH * D / V.z) * jacobian;
	vec3 bsdf = (mat.Color.xyz * (1.0f - F) * D * G * eta2 / denominator2) * (VdotH * LdotH / abs(V.z));

	return bsdf;
}

bool SampleBSDF(inout uint seed, inout BSDFSampleData data, in Material mat, in Surface surface, in HitData hitData)
{
	vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.View);

	float F0 = (1.0f - mat.eta) / (1.0f + mat.eta);
	F0 *= F0;

	float schlickWt = SchlickWeight(V.z);

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

	if (r1 < diffuseCDF)
	{
		// Diffuse
	
		data.RayDir = CosineSamplingHemisphere(seed, surface.Normal);
		vec3 L = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
		data.BSDF = EvalDiffuse(mat, L, data.PDF);
	}
	else if (r1 < metallicCDF)
	{
		// Metallic
	
		vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
		data.RayDir = normalize(reflect(-V, H));
	
		if (data.RayDir.z < 0.0f)
			return false;
	
		vec3 F = mix(mat.Color.xyz, vec3(1.0f), SchlickWeight(dot(V, H)));
		data.BSDF = EvalReflection(mat, data.RayDir, V, H, F, data.PDF);
	
		mat.Anisotropy = 0.0f;

		// Lookup table for energy compensation
		float layer = ((mat.Anisotropy + 1.0f) / 2.0f) * 32.0f;
		float energyCompensation = texture(uReflectionEnergyLookupTexture, vec3(V.z, mat.Roughness, layer)).r;
		
		data.BSDF = (1.0f + F * vec3(energyCompensation)) * data.BSDF;
	
		data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
	}
	else if (r1 < dielectricCDF)
	{
		// Dielectric
	
		vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
		data.RayDir = normalize(reflect(-V, H));
	
		if (data.RayDir.z < 0.0f)
			return false;
	
		data.BSDF = EvalReflection(mat, data.RayDir, V, H, vec3(1.0f), data.PDF);
	
		// Lookup table for energy compensation
		float layer = ((mat.Anisotropy + 1.0f) / 2.0f) * 32.0f;
		float energyCompensation = texture(uReflectionEnergyLookupTexture, vec3(V.z, mat.Roughness, layer)).r;
	
		//data.BSDF = (1.0f + vec3(1.0f) * vec3(energyCompensation)) * data.BSDF;
	
		data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
	}
	else if (r1 < glassCDF)
	{
		// Glass
	
		vec3 H = GGXSampleAnisotopic(V, mat.ax, mat.ay, Rnd(seed), Rnd(seed));
		float F = DielectricFresnel(abs(dot(V, H)), mat.eta);
	
		float r2 = Rnd(seed);
	
		if (r2 < F)
		{
			// Reflect
			data.RayDir = normalize(reflect(-V, H));
	
			if (data.RayDir.z < 0.0f)
				return false;
	
			data.BSDF = EvalReflection(mat, data.RayDir, V, H, vec3(1.0f), data.PDF);
	
			// Lookup table for energy compensation
			float layer = ((mat.Anisotropy + 1.0f) / 2.0f) * 32.0f;
			float energyCompensation = texture(uReflectionEnergyLookupTexture, vec3(V.z, mat.Roughness, layer)).r;
			data.BSDF = (1.0f + vec3(energyCompensation)) * data.BSDF;
	
			if (hitFromTheInside)
			{
				payload.InMedium = true;
				payload.MediumID = gl_InstanceCustomIndexEXT;
	
				payload.MediumColor = mat.MediumColor.rgb;
				payload.MediumDensity = mat.MediumDensity;
				payload.MediumAnisotropy = mat.MediumAnisotropy;
			}
		}
		else
		{
			// Refract
	
			data.RayDir = normalize(refract(-V, H, mat.eta));
	
			if (data.RayDir.z > 0.0f)
				return false;
	
			data.BSDF = EvalDielectricRefraction(mat, surface, data.RayDir, V, H, 0.0f, data.PDF) / data.PDF;
			data.PDF = 1.0f;
	
			// Lookup table for energy compensation
			float layer = (mat.Ior - 1.0f) * 32.0f;
	
			if (mat.eta > 1.0f)
			{
				float energyComp = texture(uRefractionEnergyLookupTextureEtaGreaterThan1, vec3(V.z, mat.Roughness, layer)).r;
	
				data.BSDF += vec3(1.0f - energyComp) * mat.Color.xyz;
			}
			else
			{
				float energyComp = texture(uRefractionEnergyLookupTextureEtaLessThan1, vec3(V.z, mat.Roughness, layer)).r;
	
				data.BSDF += vec3(1.0f - energyComp) * mat.Color.xyz;
			}
	
			if (!hitFromTheInside)
			{
				payload.InMedium = true;
				payload.MediumID = gl_InstanceCustomIndexEXT;
	
				payload.MediumColor = mat.MediumColor.rgb;
				payload.MediumDensity = mat.MediumDensity;
				payload.MediumAnisotropy = mat.MediumAnisotropy;
			}
		}
	
		data.RayDir = TangentToWorld(surface.Tangent, surface.Bitangent, surface.Normal, data.RayDir);
	}

	return true;
}

void EvaluateBSDF(in Material mat, in Surface surface, in vec3 Light, in vec3 View, out float PDF, out vec3 BSDF)
{
	vec3 L = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, Light);
	vec3 V = WorldToTangent(surface.Tangent, surface.Bitangent, surface.Normal, View);
	vec3 H = normalize(L + V);

	bool reflect = L.z >= 0.0f;

	float F0 = (1.0f - mat.eta) / (1.0f + mat.eta);
	F0 *= F0;

	float schlickWt = SchlickWeight(V.z);

	float diffuseProbability = (1.0f - mat.Metallic) * (1.0f - mat.Transparency);
	float metallicProbability = mat.Metallic;
	float dieletricProbability = (1.0f - mat.Metallic) * F0 * (1.0f - mat.Transparency);
	float glassProbability = (1.0f - mat.Metallic) * mat.Transparency;

	float inverseTotalProbability = 1.0f / (diffuseProbability + metallicProbability + dieletricProbability + glassProbability);

	diffuseProbability *= inverseTotalProbability;
	metallicProbability *= inverseTotalProbability;
	dieletricProbability *= inverseTotalProbability;
	glassProbability *= inverseTotalProbability;

	BSDF = vec3(0.0f);
	PDF = 0.0f;
	
	float tempPdf;
	if (reflect)
	{
		// Diffuse
		BSDF += EvalDiffuse(mat, L, tempPdf) * diffuseProbability;
		PDF += tempPdf * diffuseProbability;

		// Metallic
		vec3 F = mix(mat.Color.xyz, vec3(1.0f), SchlickWeight(dot(V, H)));
		vec3 metallicBSDF = EvalReflection(mat, L, V, H, F, tempPdf) * metallicProbability;

		float layer = ((mat.Anisotropy + 1.0f) / 2.0f) * 32.0f;
		float energyCompensation = texture(uReflectionEnergyLookupTexture, vec3(V.z, mat.Roughness, layer)).r;

		metallicBSDF = (1.0f + F * vec3(energyCompensation)) * metallicBSDF;

		BSDF += metallicBSDF;
		PDF += tempPdf * metallicProbability;

		// Dielectric
		vec3 dielectricReflection = EvalReflection(mat, L, V, H, vec3(1.0f), tempPdf);
		dielectricReflection = (1.0f + F * vec3(energyCompensation)) * dielectricReflection;

		BSDF += dielectricReflection * dieletricProbability;
		PDF += tempPdf * dieletricProbability;

		// Glass
		BSDF += dielectricReflection * glassProbability;
		PDF += tempPdf * glassProbability;
	}
	else
	{
		BSDF += EvalDielectricRefraction(mat, surface, L, V, H, 0.0f, tempPdf) * glassProbability;
		PDF += tempPdf * glassProbability;
	}
}

#endif