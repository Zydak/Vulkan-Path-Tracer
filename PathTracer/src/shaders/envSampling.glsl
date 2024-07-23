#ifndef sampling
#define sampling

#include "raycommon.glsl"

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
        // this texel, and renormalize the random variable for later use.
        envIdx = idx;
        xi.y /= sample_data.Importance;
    }
    else
    {
        // Otherwise we pick the alias of the texel and renormalize the random variable
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

    // Lookup the environment value using
    return texture(hdrTexture, vec2(u, v));
}

#else
#endif