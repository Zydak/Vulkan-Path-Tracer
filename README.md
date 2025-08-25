# Vulkan-Path-Tracer
![Sponza](./Gallery/GodRays.png)

Physically based offline path tracer made in Vulkan with Ray Tracing Pipeline extension. It uses energy conserving BSDF with Diffuse, Dielectric, Metallic and Glass lobes + volume scattering. Renders can also be saved as png images.

# System Requirements
- [TODO]
- Either NVIDIA RTX 2000+ series or AMD RX 6000+ series to support all of the extensions below. You may check whether they're present on your device [here](https://vulkan.gpuinfo.org/listdevices.php), maybe it's possible to run on older hardware.
- - VK_KHR_ray_query,
- - VK_KHR_acceleration_structure,
- - VK_KHR_ray_tracing_pipeline,
- - VK_KHR_swapchain,
- - VK_KHR_deferred_host_operations,
- Visual Studio 2022 (older versions might work but aren't tested).

# Running
[TODO]

# Building
[TODO]

# Features Overview

- BSDF with MIS
- Energy compensation implemented according to [[Turquin 2018]](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf) paper.
- HDR Environment Maps
- Environment map MIS
- Textures and Normal Maps
- Editor
  - Changing material and path tracing properties at runtime
  - Loading your own scenes in any format supported by [assimp](https://github.com/assimp/assimp/blob/master/doc/Fileformats.md)
  - Exporting renders into .PNG files
- Post Processing
  - Bloom using Mip Maps
  - ACES tonemapping
- Anti Aliasing
- Depth of Field effect with automatic focal length
- Volumetric scattering with MIS
- Russian roulette for faster convergence

# Gallery
![Sponza](./Gallery/GodRays.png)
![CannelleEtFromage](./Gallery/CannelleEtFromage.png)
![DragonHead](./Gallery/DragonHead.png)
![Bistro](./Gallery/Bistro.png)
![OceanAjax](./Gallery/OceanAjax.png)
![Dogs](./Gallery/Dogs.png)
![CornellBox](./Gallery/CornellBox.png)
![Mustang0](./Gallery/Mustang0.png)
![Mustang1](./Gallery/Fog.png)
![TeapotMarble](./Gallery/TeapotMarble.png)
![TeapotTiled](./Gallery/TeapotTiled.png)
![SubsurfaceBall](./Gallery/SubsurfaceBall.png)
![Caustics](./Gallery/Caustics.png)

# In Depth Project Overview

In this section I'll be going over the entire project in depth. Starting from file structure and gradually explaining each feature.

**Note**: This section describes my specific implementation choices and approach. Path tracing can be implemented in many different ways - this represents one possible solution.

## Code Structure
This path tracer is built on [VulkanHelper](https://github.com/Zydak/VulkanHelper), my vulkan abstraction to get rid of the explicitness but keep the performance and features of vulkan. I use it for all my project to minimize the boilerplate code.

The project is split into 5 main components:
- Application
- Editor
- Path Tracer
- Post Processor
- Shaders

![CodeStructureDiagram](./Gallery/Diagrams/CodeStructureDiagram.png)

Application is simple, it creates window and vulkan instance, and then delegates the rendering into the Editor component.

Editor manages the UI rendering as well as path tracer and post processor componenets. It retrieves the user input through the UI and feeds it into the path tracer and post processor to modify their behaviour.

Path tracer is an isolated component, it has no knowledge of the editor. The data to it is passed by get/set functions. This way the communication happens through these small defined channels so there is no coupling between the two, the editor can be easily swapped out. The component itself manages the path tracing, as an input it takes scene filepath and spits out path traced image as an output. It creates and manages all resources needed for path tracing (materials, cameras, mesh buffers). The only way to interact with it (apart from previously mentioned get/set functions) is calling `PathTrace()` which will schedule the work on the GPU.

Post processor also is an isolated component, it has not knowledge of the editor. As an input it takes HDR image and as output it gives post processed LDR image. The only way to interact with it is a set of get/set functions and `PostProcess()` function which does the actual post processing. Right now it doesn't do much, there's just bloom, exposure, gamma and ACES tonemapping.

Shaders are where all actual work happens. For a shading language I chose Slang since it has good compatibility with Vulkan and is generally nice to work with. The structure here is your classic RT pipeline in any API with the main 3 shaders dictating the code flow:

- `RayGen.slang` - Entry point shader that generates primary rays from camera and orchestrates the path tracing loop. It handles 2 types of intersections, geometry intersections, and AABB volume intersections. When the geometry is intersected the `ClosestHit.slang` shader is called. When AABB volume intersection is detected, it uses `Volume.slang` functionality to handle the scattering inside the volume.
- `ClosestHit.slang` - Handles surface shading, nested volumes (volumes inside meshes), and scattering direction from surface.
- `Miss.slang` & `MissShadow.slang` - Handle rays that miss geometry, mostly just samples the environment map.

Then there are Helper classes:

- `Material.slang` - Encapsulation of the material properties. Implementation of the BSDF.
- `Surface.slang` - Encapsulation of the surface data. All calculations tied to the geometry (normals and texture coords).
- `Volume.slang` - Encapsulation of AABB volumes.
- `Sampler.slang` - Sampling functions and random number generation.

and some utilities that don't really fit anywhere in particular:
- `RTCommon.slang` - Shared data structures and common ray tracing utilities that are shared across files.
- `Defines.slang` - Shader preprocessor definitions and constants.

Then there's also Lookup Table Calculator but it could really be a separate application altogether, it isn't really tied to anything and nothing is really tied to it. So I'll go over it in the [Energy Compensation] section.

## Ray Tracing Pipeline
The acceleration structure for ray tracing as well as all mesh intersection tests are handled through the Vulkan RT pipeline, and that part of the code is handled by [VulkanHelper](https://github.com/Zydak/VulkanHelper). I decided to use Vulkan RT pipeline for the simplicity and performance. It allows for utilizing RT cores on the newer GPUs so it's a lot faster than doing everything in compute. And of course you don't have to set up your own acceleration structure so it's way simpler. Although I wonder if using ray queries inside a compute shader would be faster or slower. I never got to test that out. The only thing that's worth noting here is that I do loop based approach for generating rays instead of recursion (I don't spawn new rays from hit shader), I found it around 2x-3x faster. I guess the GPU doesn't like recursion. Also with loop based approach there's no depth limit. Last time I checked vulkan only guarantess that the recursion limit is at least 1, anything above that varies per GPU.

## BSDF
For simulating surface shading materials use principled BSDF (Bidirectional scattering distribution function), which means that there is no material type per se. You edit the material properties values (like metallicness) and the lobes are blended between for you. So to put it into words nicely: it's a **multi-lobe BSDF with scalar-weighted blending**. It's useful because it allows for a lot of artistic control. I can also really easily import materials from different file formats like GLTF or OBJ, so that I don't have to roll my own format.

#### Code conventions
Each path tracer has slightly different notation and assumptions when it comes to the directions. A popular notation is $\omega_o$ for direction from a given point to a viewing point, and $\omega_i$ for the direction from a given point to the light source. But this is a backward path tracer (it traces rays from camera to the scene), so saying that incoming direction ($\omega_i$) is the direction we're actually going in seemed confusing to me. So in code, the **incoming direction** (from a point on surface to view point) is denoted as $V$ (for view), and **outgoing direction** (from a point on surface to a light source) is denoted as $L$ (for light), the same goes for this readme. Also please note that all calculations on material data inside `Material.slang` are done in **tangent space**, not world space. That's for performance reasons, in tangent space there's no need for calculating the cosine of the angle between vector and surface normal. You can get it right away with $\hat{v}.z$

### BSDF
#### Lobes
The BSDF $f(V, L)$ consists of 4 different lobes
- Diffuse
- Metallic
- Dielectric
- Glass

When ray hits the surface one of these 4 lobes is sampled stochastically based on their sampling probabilities $p_{\text{diffuse}}$, $p_{\text{metallic}}$, $p_{\text{dielectric}}$, $p_{\text{glass}}$ such that

$$
\sum_{i=1}^4 p_i = 1
$$

A direction is then sampled from the selected lobe to determine the outgoing direction $L$, and the BSDF is evaluated to determine how much radiance is reflected or refracted towards $V$ from $L$.

In code the probabilites are chosen as follows:

```
float F0 = (1.0 - material.Eta) / (1.0f + material.Eta);
F0 *= F0;

float diffuseProbability = (1.0 - material.Properties.Metallic) * (1.0 - material.Properties.Transmission);
float metallicProbability = material.Properties.Metallic;
float dielectricProbability = (1.0 - material.Properties.Metallic) * F0 * (1.0 - material.Properties.Transmission);
float glassProbability = (1.0 - material.Properties.Metallic) * material.Properties.Transmission;

// Normalize probabilities so they sum up to 1
float probabilitySum = diffuseProbability + metallicProbability + dielectricProbability + glassProbability;
diffuseProbability /= probabilitySum;
metallicProbability /= probabilitySum;
dielectricProbability /= probabilitySum;
glassProbability /= probabilitySum;
```

#### Diffuse
Diffuse is a simple lobe, I decided to use lambertian reflectance here since it's simple and looks okay. It's also perfectly energy conserving.

$$
f_{\text{diffuse}}(V, L) = \bold{C} \cdot \frac{1}{\pi}
$$

Outgoing direction $L$ is picked by sampling a cosine weighted distribution. It's done by choosing a random direction on a hemisphere based on 2 random numbers $\xi_1, \xi_2$, multiplying it by normal vector and normalizing the sum. Because the distribution is cosine weighted the PDF must also reflect that:

$$
p_{\text{diffuse}}(L) = \frac{1}{\pi} \cdot \cos\theta_L
$$

where $\cos\theta_L$ is $N \cdot L$ and $\bold{C}$ is surface base color

Also please note that in code I bake the cosine term from rendering equation directly into the BxDF calculations for simplicity. So in code you'll see additional $\cos\theta_L$ in the BxDF calculations.

So instead of

```
float3 BRDF = Properties.BaseColor * M_1_OVER_PI;
```

you'll find

```
float3 BRDF = M_1_OVER_PI * Properties.BaseColor * L.z;
```

the same goes for all other lobes.

#### Metallic
Metallic lobe is using anisotropic GGX distribution implemented according to [Sampling the GGX Distribution of Visible Normals by Eric Heitz](https://jcgt.org/published/0007/04/01/paper.pdf).

First the microsurface normal $H$ has to be sampled, for that please refer to the paper above, it even provides full code for the sampling function. The most important thing there is that it uses VNDF given by

$$
\text{VNDF}(V, H) = \frac{G_1(V)\; \text{max}(0, V \cdot H)\; D(H)}{\cos\theta_V}
$$

That is the probability of sampling the microsurface normal given $V$.

After $H$ has been sampled $L$ can be computed as

$$
L = \text{reflect}(-V, H)
$$

With $V$, $L$ and $H$ in place the BRDF can be evaluated.

$$
f_{\text{metallic}}(V, L) = \frac{F(V, H) D(H) G(V, L)}{4 \cos\theta_V \cos\theta_L}
$$

where

$$
D(H) = \frac{1}{\pi \alpha_x \alpha_y (\frac{x_h^2}{\alpha_x^2} + \frac{y_h^2}{\alpha_y^2} + z_n^2)^2}
$$

$$
G(V, L) = G_1(V) \cdot G_1(L)
$$

$$
G_1(\hat{v}) = \frac{1}{1 + \Lambda(\hat{v})} \text{, where }
\Lambda(\hat{v}) = \frac{-1 + \sqrt{1 + \frac{\alpha_x^2 x_{\hat{v}}^2+\alpha_y^2 y_{\hat{v}}^2}{z_{\hat{v}}^2}}}{2} 
$$

and for fresnel I use a simple schlick approximation.

$$
F(\cos\theta, F_0) = F_0 + (1 - F_0)(1 - \cos\theta)^5 \text{, where } F_0 = \bold{C}
$$
Finally, the PDF is given by weighting VNDF by the jacobian of reflect operator.

$$
p_\text{metallic}(V, L, H) = \frac{\text{VNDF}}{4\; (V \cdot H)}
$$

#### Dielectric


# References

## Papers Implemented
- [Sampling the GGX Distribution of Visible Normals](https://jcgt.org/published/0007/04/01/paper.pdf)
- [Microfacet Models for Refraction through Rough Surfaces](https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf)
- [Importance Sampling Microfacet-Based BSDFs using the Distribution of Visible Normals](https://inria.hal.science/hal-00996995v2/document)
- [A Reflectance Model For Computer Graphics](https://dl.acm.org/doi/pdf/10.1145/357290.357293)
- [Practical multiple scattering compensation for microfacet models](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf)
- [Production Volume Rendering 2017](https://graphics.pixar.com/library/ProductionVolumeRendering/paper.pdf)

## Models
- https://developer.nvidia.com/orca/amazon-lumberyard-bistro - Bistro
- https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html - Sponza
- https://sketchfab.com/3d-models/screaming-dragon-head-3d-print-5712b52618f743b193bdd39459099f25 - Screaming Dragon Head
- https://sketchfab.com/3d-models/dog-statue-49d97ca2fbf34f85b6c88ae8ebc7514f - Dog Statue
- https://github.com/mmacklin/tinsel - Ajax
- https://polyhaven.com/hdris - Env Maps
- https://benedikt-bitterli.me/resources/ - Dragon
- https://wirewheelsclub.com/models/1965-ford-mustang-fastback/ - Mustang
- https://renderman.pixar.com/official-swatch - RenderMan teapot
- https://www.cgbookcase.com/ - Textures for teapots
- https://benedikt-bitterli.me/resources/ - Material Test Ball
- https://sketchfab.com/3d-models/