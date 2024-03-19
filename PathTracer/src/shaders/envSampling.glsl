#ifndef sampling
#define sampling

#include "raycommon.glsl"

float T2P(in float t, in int noOfPixels)
{
    return t * float(noOfPixels) - 0.5;
}

float Halton(uint base, uint index)
{
    float result = 0.0;
    float digitWeight = 1.0;
    while (index > 0u)
    {
        digitWeight = digitWeight / float(base);
        uint nominator = index % base; // compute the remainder with the modulo operation
        result += float(nominator) * digitWeight;
        index = index / base;
    }
    return result;
}

vec3 SampleInverseTransformEnvMap(sampler2D envMapSampler, sampler2D jointPDFSampler, sampler2D CDFXInvSampler, sampler2D CDFYInvSampler, in vec3 normal)
{
	vec3 result = vec3(0.0f);
	vec2 texCoord = directionToSphericalEnvmap(normal);
	vec2 textureSize = textureSize(envMapSampler, 0);

	float px = T2P(texCoord.x, int(textureSize.x));
	float py = T2P(texCoord.y, int(textureSize.y));

	uint N = 20;
	for (uint n = 1u; n <= N; n++)
	{
		vec2 random = vec2(Halton(2u, n), Halton(3u, n));
		float sampleY = texture(CDFXInvSampler, vec2(0.5, random.y)).r;
		float sampleX = texture(CDFYInvSampler, vec2(random.x, sampleY)).r;
		vec2 sampleLocation = vec2(sampleX, 1.0f - sampleY);

		vec3 radiance = texture(envMapSampler, sampleLocation).rgb;
		float pdf = texture(jointPDFSampler, sampleLocation).r;
		vec3 posWorld = sphericalEnvmapToDirection(sampleLocation);
		float cosTheta = dot(normal, posWorld);
		if (cosTheta > 0.0f && pdf > 0.0f)
		{
			float theta = M_PI * (1.0 - sampleLocation.t);
			result += 2.0f * M_PI * radiance * cosTheta * sin(theta) / pdf;
		}
	}

	return result / float(N);
}

#ifdef SAMPLE_IMPORTANCE
vec4 SampleImportanceEnvMap(in sampler2D hdrTexture, in vec3 randVal, out vec3 toLight)
{
    // Uniformly pick a texel index idx in the environment map
    vec3  xi = randVal;
    uvec2 tsize = uvec2(textureSize(hdrTexture, 0));
    uint  width = tsize.x;
    uint  height = tsize.y;

    uint size = width * height;
    uint idx = min(uint(xi.x * float(size)), size - 1);

    // Fetch the sampling data for that texel, containing the importance and the texel alias
    EnvAccel sample_data = uAccels[idx];

    uint envIdx;

    if (xi.y < sample_data.Importance)
    {
        // If the random variable is lower than the importance, we directly pick
        // this texel, and renormalize the random variable for later use. The PDF is the
        // one of the texel itself
        envIdx = idx;
        xi.y /= sample_data.Importance;
    }
    else
    {
        // Otherwise we pick the alias of the texel, renormalize the random variable and use
        // the PDF of the alias
        envIdx = sample_data.Alias;
        xi.y = (xi.y - sample_data.Importance) / (1.0f - sample_data.Importance);
    }

    // Compute the 2D integer coordinates of the texel
    const uint px = envIdx % width;
    uint       py = envIdx / width;

    // Uniformly sample the solid angle subtended by the pixel.
    // Generate both the UV for texture lookup and a direction in spherical coordinates
    const float u = float(px + xi.y) / float(width);
    const float phi = u * (2.0f * M_PI) - M_PI;
    float       sin_phi = sin(phi);
    float       cos_phi = cos(phi);

    const float step_theta = M_PI / float(height);
    const float theta0 = float(py) * step_theta;
    const float cos_theta = cos(theta0) * (1.0f - xi.z) + cos(theta0 + step_theta) * xi.z;
    const float theta = acos(cos_theta);
    const float sin_theta = sin(theta);
    const float v = theta * M_1_OVER_PI;

    // Convert to a light direction vector in Cartesian coordinates
    toLight = vec3(cos_phi * sin_theta, cos_theta, sin_phi * sin_theta);
    toLight = Rotate(toLight, vec3(1, 0, 0), push.EnvAltitude);
    toLight = Rotate(toLight, vec3(0, 1, 0), push.EnvAzimuth);

    // Lookup the environment value using bilinear filtering
    return texture(hdrTexture, vec2(u, v));
}
#endif

#else
#endif