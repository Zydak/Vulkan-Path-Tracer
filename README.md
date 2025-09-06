# Vulkan-Path-Tracer
![Sponza](./Gallery/GodRays.png)

Physically based offline path tracer made in Vulkan with Ray Tracing Pipeline extension. It uses energy conserving BSDF with Diffuse, Dielectric, Metallic and Glass lobes + volumetric scattering. Renders can also be saved as png images.

# System Requirements
- Windows 10 with MSVC or Linux with GCC (Only debian 12 with GCC 12.2 is tested)
- Either NVIDIA RTX 2000+ series or AMD RX 6000+ series to support all of the extensions below. You may check whether they're present on your device [here](https://vulkan.gpuinfo.org/listdevices.php), maybe it's possible to run on some older hardware.
  - VK_KHR_ray_query
  - VK_KHR_acceleration_structure
  - VK_KHR_ray_tracing_pipeline
  - VK_KHR_swapchain
  - VK_KHR_deferred_host_operations

# Building
## Prerequisites
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home).
- Cmake 3.12
- C++ 20

## Windows
```
git clone --recursive https://github.com/Zydak/Vulkan-Path-Tracer
cd Vulkan-Path-Tracer
mkdir build
cd build
cmake ..
```
Then open generated Visual Studio solution and build.

## Linux
```
git clone --recursive https://github.com/Zydak/Vulkan-Path-Tracer
cd Vulkan-Path-Tracer
mkdir build
cd build
cmake ..
make
```
Executable will be in `build/PathTracer/VulkanPathTracer`.

# Features Overview

- BSDF with importance sampling
- Energy compensation implemented according to [[Turquin 2018]](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf) paper.
- HDR Environment Maps with importance sampling
- NEE for environment map light
- Volumetric scattering with importance sampling implemented according to [Production Volume Rendering 2017](https://graphics.pixar.com/library/ProductionVolumeRendering/paper.pdf)
- Multiple Importance Sampling implemented according to [Optimally Combining Sampling Techniques for Monte Carlo Rendering](https://www.cs.jhu.edu/~misha/ReadingSeminar/Papers/Veach95.pdf)
- Textures and Normal Maps
- Editor
  - Changing material and path tracing properties at runtime
  - Loading your own scenes in any format supported by [assimp](https://github.com/assimp/assimp/blob/master/doc/Fileformats.md)
  - Exporting renders into .PNG files
- Post Processing
  - Bloom using Mip Maps
  - ACES tonemapping
- Anti Aliasing
- Depth of Field
- Russian roulette

If you're interested in details of the implementation of these features, look at [Project Overview](https://zydak.github.io/Vulkan-Path-Tracer/).

# Gallery
<p align="center">

<img src="./Gallery/GodRays.png"/>
<img src="./Gallery/CannelleEtFromage.png"/>
<img src="./Gallery/DragonHead.png"/>
<img src="./Gallery/Bistro.png"/>
<img src="./Gallery/OceanAjax.png"/>
<img src="./Gallery/Dogs.png"/>
<img src="./Gallery/BreakfastRoom.png"/>
<img src="./Gallery/CornellBox.png"/>
<img src="./Gallery/VolumeLight.png"/>
<img src="./Gallery/Mustang0.png"/>
<img src="./Gallery/FogCarUndenoised.png"/>
<img src="./Gallery/TeapotMarble.png"/>
<img src="./Gallery/TeapotTiled.png"/>
<img src="./Gallery/SubsurfaceBall.png"/>
<img src="./Gallery/Caustics.png"/>

</p>


# References

## Papers Implemented
- [Sampling the GGX Distribution of Visible Normals](https://jcgt.org/published/0007/04/01/paper.pdf)
- [Microfacet Models for Refraction through Rough Surfaces](https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf)
- [Importance Sampling Microfacet-Based BSDFs using the Distribution of Visible Normals](https://inria.hal.science/hal-00996995v2/document)
- [A Reflectance Model For Computer Graphics](https://dl.acm.org/doi/pdf/10.1145/357290.357293)
- [Practical multiple scattering compensation for microfacet models](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf)
- [Production Volume Rendering 2017](https://graphics.pixar.com/library/ProductionVolumeRendering/paper.pdf)
- [Optimally Combining Sampling Techniques for Monte Carlo Rendering](https://www.cs.jhu.edu/~misha/ReadingSeminar/Papers/Veach95.pdf)
- [Scratch a pixel article on volumes](https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/monte-carlo-methods-in-practice/monte-carlo-simulation.html)

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
- https://casual-effects.com/g3d/data10/ - Breakfast Room
- https://luxcorerender.org/example-scenes/ - Cannele & Fromage
