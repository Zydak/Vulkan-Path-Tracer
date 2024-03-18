# Vulkan-Path-Tracer

Vulkan Path Tracer is an offline PBR renderer made using the Vulkan API, designed for simulating global illumination and complex lighting effects, featuring a lot of post-processing effects. Renders that you create can be easly saved to disk as PNG images.

# Gallery
![Monster](./Gallery/MonsterRough.png)
![MonsterInk](./Gallery/MonsterInk.png)
![MonsterPosterize](./Gallery/MonsterPosterize.png)
![Dragon](./Gallery/DisneyDragon.png)
![CornellBox](./Gallery/CornellBox.png)

# Features
- Complex Materials
  - Albedo, Metallic and Roughness Textures
  - Translucent materials with IOR
  - Clearcoat
  - Emissive Factor
- Editor
  - Loading your own scenes in .gltf and .obj format
  - Editing scene materials in runtime
- Exporting renders into .PNG files
- Shader reloading and caching
- HDR Environment Maps with importance sampling
- Post Processing
  - Bloom using Mip Maps
  - Exposure
  - Contrast
  - Brightness
  - Saturation
  - Vignette
  - Gamma
  - Temperature with Tint
  - Color Filter
  - Chromatic Aberration
  - 6 Tonemappers (Filmic, Hill Aces, Narkowicz Aces, Exposure Mapping, Uncharted 2, Reinchard Extended)
  - Ink Effect: Detects Edges, turns everything to black and white, applies blue noise and adds paper texture.
  - Posterize Effect: Quantizes Colors, Applies Dithering and adds ability to replace color pallet with your own colors or colors generated in OKLAB color space
- Physically Accurate BSDF
- GPU Path Tracing using Vulkan Ray Tracing Pipeline
  - Albedo, Glossy Reflections, Glass
  - Fireflies Elimination
- Anti Aliasing
- Depth of Field effect with automatic focal length
- Image Denoising using Nvidia Optix Denoiser
- Camera made using quaternions