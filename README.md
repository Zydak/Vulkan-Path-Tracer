# Vulkan-Path-Tracer
![Sponza](./Gallery/GodRays.png)

Physically based offline path tracer made in Vulkan with Ray Tracing Pipeline extension. It uses energy conserving BSDF with Diffuse, Dielectric, Metallic and Glass lobes + volume scattering. Renders can also be saved as png images.

# System Requirements
- [TODO]
- Either NVIDIA RTX 2000+ series or AMD RX 6000+ series to support all of the extensions below. You may check whether they're present on your device [here](https://vulkan.gpuinfo.org/listdevices.php), maybe it's possible to run on older hardware.
  - VK_KHR_ray_query
  - VK_KHR_acceleration_structure
  - VK_KHR_ray_tracing_pipeline
  - VK_KHR_swapchain
  - VK_KHR_deferred_host_operations
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
![Fog](./Gallery/Fog.png)
![TeapotMarble](./Gallery/TeapotMarble.png)
![TeapotTiled](./Gallery/TeapotTiled.png)
![SubsurfaceBall](./Gallery/SubsurfaceBall.png)
![Caustics](./Gallery/Caustics.png)

# In Depth Project Overview

**Disclaimer**: This section describes my specific implementation choices and approach to path tracing. I'll be going over the entire project in depth. Starting from file structure and gradually explaining each feature. The mathematical formulations and techniques described here represent my interpretation and implementation of various published papers and methods. But for authoritative and complete information, please refer to the original papers cited throughout this readme.

## Code Structure
This path tracer is built on [VulkanHelper](https://github.com/Zydak/VulkanHelper), my vulkan abstraction to get rid of the explicitness but keep the performance and features of vulkan. I use it for all my projects to minimize the boilerplate code.

The project is split into 5 main components:
- Application
- Editor
- Path Tracer
- Post Processor
- Shaders

![CodeStructureDiagram](./Gallery/Diagrams/CodeStructureDiagram.png)

Application is simple, it creates window and vulkan instance, and then delegates the rendering into the Editor component.

Editor manages the UI rendering as well as path tracer and post processor components. It retrieves the user input through the UI and feeds it into the path tracer and post processor to modify their behaviour.

Path tracer is an isolated component, it has no knowledge of the editor. The data to it is passed by get/set functions. This way the communication happens through these small defined channels so there is no coupling between the two, the editor can be easily swapped out. The component itself manages the path tracing, as an input it takes scene filepath and spits out path traced image as an output. It creates and manages all resources needed for path tracing (materials, cameras, mesh buffers). The only way to interact with it (apart from previously mentioned get/set functions) is calling `PathTrace()` which will schedule the work on the GPU.

Post processor also is an isolated component, it has no knowledge of the editor. As an input it takes HDR image and as output it gives post processed LDR image. The only way to interact with it is a set of get/set functions and `PostProcess()` function which does the actual post processing. Right now it doesn't do much, there's just bloom, exposure, gamma and ACES tonemapping.

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
The acceleration structure for ray tracing as well as all mesh intersection tests are handled through the Vulkan RT pipeline, and that part of the code is handled by [VulkanHelper](https://github.com/Zydak/VulkanHelper). I decided to use Vulkan RT pipeline for the simplicity and performance. It allows for utilizing RT cores on the newer GPUs so it's a lot faster than doing everything in compute. And of course you don't have to set up your own acceleration structure so it's way simpler. Although I wonder if using ray queries inside a compute shader would be faster or slower. I never got to test that out. The only thing that's worth noting here is that I do loop based approach for generating rays instead of recursion (I don't spawn new rays from hit shader), I found it around 2x-3x faster. I guess the GPU doesn't like recursion. Also with loop based approach there's no depth limit. Last time I checked vulkan only guarantees that the recursion limit is at least 1, anything above that varies per GPU.

## BSDF
Materials use a principled BSDF (Bidirectional Scattering Distribution Function), which means that there is no material type per se. You edit the material property values (like metallic) and the lobes are blended between for you. So to put it into words nicely: it's a **multi-lobe BSDF with scalar-weighted blending**. This approach is useful because it allows for a lot of artistic control. I can also really easily import materials from different file formats like glTF or OBJ, so that I don't have to roll my own format.

Currently supported material properties are:
- Base Color
- Emissive Color
- Specular Color
- Metallic
- Roughness
- IOR
- Transmission
- Anisotropy
- Anisotropy Rotation
- Base Color Texture
- Normal Texture
- Metallic Texture
- Roughness Texture
- Emissive Texture

### Code conventions
Each path tracer has slightly different notation and assumptions when it comes to the directions. A popular notation is $\omega_o$ for direction from a given point to a viewing point, and $\omega_i$ for the direction from a given point to the light source. But this is a backward path tracer (it traces rays from camera to the scene), so saying that incoming direction ($\omega_i$) is the direction we're actually going in seemed confusing to me. So in my code, the **incoming direction** (from a point on surface to view point) is denoted as $\mathbf{V}$ (for view), and **outgoing direction** (from a point on surface to a light source) is denoted as $\mathbf{L}$ (for light), the same notation is used throughout this readme. Also please note all calculations in code on material data inside `Material.slang` are done in **tangent space**, not world space. I chose this for performance reasons, in tangent space there's no need for calculating the cosine of the angle between vector and surface normal. You can get it right away with $\hat{v}_z$.

### Microsurface

I wanted the path tracer to be physically based, so I use microfacet theory to simulate surface scattering. When a ray hits the surface, the microsurface $\mathbf{H}$ is sampled according to the VNDF for GGX. Sampling implementation follows the method described in [Sampling the GGX Distribution of Visible Normals](https://jcgt.org/published/0007/04/01/paper.pdf).

$$
\text{VNDF} = \frac{G_1(\mathbf{V}) \cdot \text{max}(0, \mathbf{V} \cdot \mathbf{H})\cdot D}{\mathbf{V} \cdot \mathbf{N}}
$$

### Lobes
I split materials into 3 different types
- Metallic
- Dielectric
- Glass

When a ray hits the surface, one of these 3 types is sampled stochastically based on their sampling weights $w_{\text{metallic}}$, $w_{\text{dielectric}}$, $w_{\text{glass}}$. These weights are chosen more or less arbitrarily and then are normalized so that they sum up to 1.

A direction is then sampled from the selected type to determine the outgoing direction $\mathbf{L}$, and the BSDF is evaluated to determine how much radiance is reflected or refracted towards $\mathbf{V}$ from $\mathbf{L}$.

### Sampling
The BSDF is divided into two parts, sampling the direction (`SampleBSDF(V, H, F)`) and evaluation of that direction (`EvaluateBSDF(V, H, L, F)`). First let's focus on sampling the outgoing direction.

#### Metallic
The sampling weight is simple here: $w_\text{metallic} = metallic$.
The outgoing direction $\mathbf{L}$ can be computed as

$$
\mathbf{L} = \text{reflect}(-\mathbf{V}, \mathbf{H})
$$

Note that it is possible for $\mathbf{L}$ to go below the surface. In that case I discard the sample. That leads to an energy loss which I then fix with energy compensation lookup tables so that everything is energy conserving. The alternative would be to properly simulate multiple surface scattering according to [Heitz 2016](https://jo.dreggn.org/home/2016_microfacets.pdf). I'll touch on that later.

#### Dielectric

Sampling weight is $w_\text{dielectric} = (1 - metallic) * (1 - transmission)$.

Unlike metals, where I only simulate reflection, the dielectrics are a little bit more complicated. I basically simulate 2 cases here: the light can either reflect from the surface, or transmit into it.

The probability of ray being reflected is given by the fresnel equation

$$
\begin{gather*}
\text{Given:} \quad \eta = \frac{n_i}{n_t}, \quad \cos\theta_i = \mathbf{V} \cdot \mathbf{H} \\
\sin^2\theta_t = \eta^2 \left(1 - \cos^2\theta_i\right) \\
\text{If } \sin^2\theta_t > 1: \quad F_D = 1\\
\text{Else:} \quad \cos\theta_t = \sqrt{1 - \sin^2\theta_t} \\
r_s = \frac{\eta \cos\theta_t - \cos\theta_i}{\eta \cos\theta_t + \cos\theta_i} \\
r_p = \frac{\eta \cos\theta_i - \cos\theta_t}{\eta \cos\theta_i + \cos\theta_t} \\
F_D = \frac{1}{2} \left( r_s^2 + r_p^2 \right)
\end{gather*}
$$

A random value $\xi \sim \mathcal{U}(0, 1)$ is sampled and

$$
\begin{gather*}
\text{If} \quad \xi < F_D: \quad \text{Reflect} \\
\text{Else}: \quad \text{Transmit}
\end{gather*}
$$

If ray got reflected, outgoing direction is computed the same way as for metallic.

$$
\mathbf{L} = \text{reflect}(-\mathbf{V}, \mathbf{H})
$$

If ray got transmitted, I scatter it diffusely, for that I use Lambertian reflection. Outgoing direction $\mathbf{L}$ is computed by sampling a random vector on a hemisphere with cosine weighted distribution.

###

#### Glass
Sampling weight is $w_\text{glass} = (1 - metallic) * transmission$.

Ideally, I could have implemented glass as part of the dielectric (since glass is also a dielectric material), then I could choose between scattering diffusely and refracting based on material's $\text{transmission}$ value, but I had to make it a separate thing due to a constraint with the energy compensation system.

The problem is that the [[Turquin 2019]](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf) paper doesn't provide a method to calculate energy compensation separately for just the transmission component, it only gives the combined reflection + transmission compensation. According to it, the energy compensation lookup tables need to account for all possible light paths, and for refractive materials like glass, this includes both reflected and transmitted rays: $E_\text{ss}^S = E_\text{ss}^R + E_\text{ss}^T$. This means I need to apply the same energy compensation to both the reflection and transmission parts of the glass BSDF. So reflecting ray requires knowing whether the material will be refractive or not. I have to know whether to apply only reflection compensation (like in dielectric) or reflection + transmission compensation.

I had an attempt at making refractive only lookup table but it failed miserably. I'm not really sure whether it's not possible at all or I had made some mistake along the way because they didn't really expand on that in the paper. So anyway, that's why I have glass as a separate type alongside dielectric. I hope that made any sense.

To determine whether the ray is reflected or refracted I use the same logic as in dielectric, a random variable $\xi \sim \mathcal{U}(0, 1)$ is sampled and

$$
\begin{gather*}
\text{If} \quad \xi < F_D: \quad \text{Reflect}\\
\text{Else}: \quad \text{Refract}
\end{gather*}
$$

If ray got reflected, outgoing direction is computed the same way as for dielectric and metallic.
$$
\begin{gather*}
\mathbf{L} = \text{reflect}(-\mathbf{V}, \mathbf{H})\\
\end{gather*}
$$

And for refraction, instead of $\text{reflect}$, $\text{refract}$ is called.

$$
\mathbf{L} = \text{refract}(-\mathbf{V}, \mathbf{H}, \eta) \\
$$

### Evaluation
With $\mathbf{V}$, $\mathbf{H}$ and $\mathbf{L}$ in place the BSDF can be evaluated. Everything here is based on [Sampling the GGX Distribution of Visible Normals](https://dl.acm.org/doi/pdf/10.1145/357290.357293) & [Microfacet Models for Refraction through Rough Surfaces](https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf).

#### Metallic

$$
f_{\text{metallic}} = \frac{F \cdot D \cdot G}{4 (\mathbf{V} \cdot \mathbf{N})(\mathbf{V} \cdot \mathbf{L})}
$$

where

$$
D = \frac{1}{\pi \alpha_x \alpha_y (\frac{x_h^2}{\alpha_x^2} + \frac{y_h^2}{\alpha_y^2} + z_n^2)^2}
$$

$$
G = G_1(\mathbf{V}) \cdot G_1(\mathbf{L})
$$

$$
G_1(\hat{v}) = \frac{1}{1 + \Lambda(\hat{v})} \text{, where }
\Lambda(\hat{v}) = \frac{-1 + \sqrt{1 + \frac{\alpha_x^2 x_{\hat{v}}^2+\alpha_y^2 y_{\hat{v}}^2}{z_{\hat{v}}^2}}}{2} 
$$

Now for fresnel, I don't have complex indices of refraction, so I decided to just do what Blender does: blend between surface base color and specular tint color based on Schlick fresnel approximation.

$$
F = \text{lerp}(\mathbf{C}, \mathbf{S}, 1 - \mathbf{V} \cdot \mathbf{H})^5
$$

The PDF is given by weighting VNDF (probability of sampling $\mathbf{H}$ given direction $\mathbf{V}$) by the jacobian of the reflect operator.

$$
p_\text{metallic} = \frac{\text{VNDF}}{4 (\mathbf{V} \cdot \mathbf{H})}
$$

#### Dielectric

Reflection is evaluated in pretty much the same way as metallic.

$$
\begin{gather*}
f_{\text{dielectric}}^R = \frac{F \cdot D \cdot G}{4 (\mathbf{V} \cdot \mathbf{N}) (\mathbf{L} \cdot \mathbf{N})}\\
p_\text{dielectric}^R = \frac{\text{VNDF}}{4 (\mathbf{V} \cdot \mathbf{H})}
\end{gather*}
$$

with the only difference being that instead of using Schlick, the $F$ factor gets changed to the specular tint color of the surface.

$$
F = \text{specularTint}
$$

It isn't equal to $F_D$ because the actual fresnel equation is already included in the sampling probability, so I use $F$ factor in the equation just for tinting the color.

And refraction is just simple Labertian reflection so:

$$
\begin{gather*}
f_\text{dielectric}^T = \mathbf{C} \cdot \frac{1}{\pi} \\
p_\text{dielectric}^T = \frac{\mathbf{L} \cdot \mathbf{N}}{\pi}
\end{gather*}
$$

Where $\mathbf{C}$ is the surface base color.

#### Glass

BRDF and PDF for reflection stay the same as in dielectric, nothing is different here.
$$
\begin{gather*}
f_{\text{glass}}^R = \frac{F \cdot D \cdot G}{4 (\mathbf{V} \cdot \mathbf{N}) (\mathbf{L} \cdot \mathbf{N})}\\
p_\text{glass}^R = \frac{\text{VNDF}}{4 (\mathbf{V} \cdot \mathbf{H})}\\
F = \text{specularTint}
\end{gather*}
$$

For refraction, instead of BRDF, BTDF is computed

$$
f_\text{glass}^T = \frac{|\mathbf{V} \cdot \mathbf{H}| |\mathbf{L} \cdot \mathbf{H}|}{|\mathbf{V} \cdot \mathbf{N}| |\mathbf{L} \cdot \mathbf{N}|} \cdot \frac{\eta^2 \cdot F \cdot G \cdot D}{(\eta(\mathbf{V} \cdot \mathbf{H}) + (\mathbf{L} \cdot \mathbf{H}))^2}
$$

With fresnel being the surface base color since I want the color to be tinted on refraction.

$$
F = \mathbf{C}
$$

The PDF also slightly changes, we still use the same VNDF but this time instead of weighting by the jacobian of $\text{reflect}$ we weight by the jacobian of $\text{refract}$

$$
p_\text{glass}^T = \frac{\text{VNDF}}{\frac{\eta^2 |\mathbf{L} \cdot \mathbf{H}|}{(\eta(\mathbf{V} \cdot \mathbf{H}) + \mathbf{L} \cdot \mathbf{H})^2}}
$$

#### Final BSDF

After every lobes' BxDF and PDF have been evaluated they have to be combined. For that I multiply each BxDF and PDF by their respective probabilities of being sampled and then add them all together.

$$
\begin{gather*}
f = f_\text{metallic} \cdot w_\text{metallic} + f_\text{dielectric}^R \cdot w_\text{dielectric} \cdot F_D + f_\text{dielectric}^T \cdot w_\text{dielectric} \cdot (1 - F_D) + f_\text{glass}^R \cdot w_\text{glass} \cdot F_D + f_\text{glass}^T \cdot w_\text{glass} \cdot (1 - F_D)\\
p = p_\text{metallic} \cdot w_\text{metallic} + p_\text{dielectric}^R \cdot w_\text{dielectric} \cdot F_D + p_\text{dielectric}^T \cdot w_\text{dielectric} \cdot (1 - F_D) + p_\text{glass}^R \cdot w_\text{glass} \cdot F_D + p_\text{glass}^T \cdot w_\text{glass} \cdot (1 - F_D)
\end{gather*}
$$

And that gives me the final BSDF $f$ and it's PDF $p$.

That's how the entire BSDF presents:
![BSDF](./Gallery/BSDF.png)

## Energy compensation
My GGX implementation is not energy conserving, that's because of 2 reasons. First when the $L$ is sampled, it is possible for ray to bounce into the surface instead of out of it (or the other way around for refraction). In that case I just discard the sample, which means that the energy is lost completely. And the second reason, the masking function discards light occluded by other microfacets. That's bad because increasing roughness of a surface introduces visible darkening of the color. This is especially visible in rough glass where light bounces multiple times.

<p align="center">
  <img src="./Gallery/MetalNoCompensation.png" width="400" />
  <img src="./Gallery/GlassNoCompensation.png" width="400" />
</p>

<p align="center">
  <img src="./Gallery/FurnaceMetalNoCompensation.png" width="400" />
  <img src="./Gallery/FurnaceGlassNoCompensation.png" width="400" />
</p>

One way to fix this is simulating multiple surface scattering, accounting for the fact that light can bounce multiple times, just like [[Heitz 2016]](https://jo.dreggn.org/home/2016_microfacets.pdf) suggests. The problem is that: 1. it's not that easy to implement, and 2. according to [[Turquin 2019]](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf) properly simulating multiple scattering can be from 7x to even 15x slower. So instead I decided to use energy compensation lookup tables implemented according to [[Turquin 2019]](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf). They're easy to compute and implement, but most importantly, they're fast.

### Lookup Energy Calculator
The CPU code for generating LUTs (Lookup Tables) is in the `LookupTableCalculator.cpp`. The class itself is pretty simple, you give it a shader alongside the LUT size, it executes that shader repeatedly until all samples have been accumulated, and then it returns the lookup table as a vector of floats. These floats are then cached on disk inside `Assets/LookupTables` as binary files and later loaded as textures for the path tracer to use. It could be just easily implemented on the CPU but of course it would be much much slower.

#### Reflection LUT
The first LUT is the reflection LUT, it's used to compensate metallic and dielectric lobes, since they are reflection only. Code for computing the energy loss is in `LookupReflect.slang`. To compute the amount of energy lost, a dummy surface and material are created. Based on the direction $V$ determined by the x coordinate in the LUT, and roughness that's determined by y coordinate in the LUT, the microsurface $H$ and outgoing direction $L$ are sampled. Then the GGX reflection is evaluated, and the amount of energy that left the surface is saved into a buffer. This process is repeated millions of times for each pixel in the LUT to get a somewhat accurate estimate of the amount of energy lost on average. I decided to use 64x64x32 LUT for the reflection. After taking 10 million samples per pixel (1310720000000 in total) I ended with this:

<p align="center">
  <img src="./Gallery/ReflectionLookup.png" width="400" />
</p>

X axis represents viewing angle ($\mathbf{V} \cdot \mathbf{N}$) and Y axis represents surface roughness. As you can see, most energy is lost at grazing angles with high roughness (Lower right corner, both X and Y are high, since (0, 0) is left top corner).

But the reflection LUT is 3 dimensional, and the third parameter is anisotropy, but this one is tricky, because the energy loss is dependent on the viewing direction, not just angle this time. So to properly compute energy loss for anisotropy I'd actually need to add even more dimensions to the table. But I decided not to do that, the LUT still gets most of the energy from anisotropy back, and the anisotropy itself is used so rarely that I decided it's not really worth it.

#### Glass LUT
Glass LUT is computed in a similar fashion with a couple of small differences. First, instead of computing the energy lost during reflection, the energy loss during both reflection and refraction is computed. Second, the LUT has to also be parameterized by IOR, so the third dimension of the LUT is IOR instead of anisotropy this time. And lastly, 2 different LUTs have to be computed for glass, the differentiation between ray hitting the surface from inside the mesh and ray hitting the surface from outside the mesh has to be made. That's because IOR changes based on that fact. I decided to use 128x128x32 LUT this time because the glass needs a lot more precision than simple reflection. Also x coordinate is now parameterized with $(\mathbf{V} \cdot \mathbf{N})^2$ because more precision is needed on grazing angles. The code can be found in `LookupRefract.slang`. After accumulating 10 million samples per pixel (5242880000000) in total I get this:

<p align="center">
  <img src="./Gallery/RefractionLookupInside.png" width="400" />
  <img src="./Gallery/RefractionLookupOutside.png" width="400" />
</p>

First image represents LUT for ray coming from inside the mesh while the second represents the ray coming from outside. Both are slices of the third dimension with IOR 1.5.

#### Compensation
After getting the tables the rest is simple, just use the equations from the paper:

For metallic and dielectric reflection:

$$
f_\text{ms} = (1 + F_0 \cdot \frac{1 - E_\text{ss}}{E_\text{ss}}) \cdot f_\text{ss}
$$

with $E_\text{ss}$ being the value from the LUT.

and for glass

$$
\begin{gather*}
f_\text{ms}^R = \frac{f_\text{ss}^R}{E_\text{ss}}\\
f_\text{ms}^T = \frac{f_\text{ss}^T}{E_\text{ss}}
\end{gather*}
$$

And that's it.

<p align="center">
  <img src="./Gallery/MetalCompensation.png" width="400" />
  <img src="./Gallery/GlassCompensation.png" width="400" />
</p>

<p align="center">
  <img src="./Gallery/FurnaceMetalCompensation.png" width="400" />
  <img src="./Gallery/FurnaceGlassCompensation.png" width="400" />
</p>

Now, the metallic furnace test is pretty much indistinguishable without turning up the contrast, but in the glass furnace test, if you look closely, you'll see that the compensation is not perfect. That's because the tables have limited precision, and a limited number of samples are taken, and that's causing some issues down the line. But that's okay, the couple percent of energy loss or gain are barely visible even in the furnace test, let alone in complex scenes, and the simplicity of the solution along with its speed make it a much more preferable option than [[Heitz 2016]](https://jo.dreggn.org/home/2016_microfacets.pdf) approach. Making path tracer 100% energy conserving and preserving has almost no benefits. And the amount of performance that's sacrificed in the process is very noticeable. The only important thing to me, is that there is no longer any color darkening. Rough glass was impossible to simulate since it turned black really fast. And the color on the metal surface was very saturated and darkened. Now there's none of that. So the key point is that both problems are solved.

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
