## Code Structure
This path tracer is built on [VulkanHelper](https://github.com/Zydak/VulkanHelper), my vulkan abstraction layer to get rid of the explicitness but keep the performance and features of vulkan. I use it for all my projects to minimize the boilerplate code.

The project is split into 5 main components:
- Application
- Editor
- Path Tracer
- Post Processor
- Shaders

<p align="center">
  <img src="../Gallery/Diagrams/CodeStructureDiagram.png"/>
</p>

Application is simple, it creates window and vulkan instance, and then leaves the rendering to the Editor component.

Editor manages the UI rendering as well as path tracer and post processor components. It retrieves the user input through the UI and feeds it into the path tracer and post processor to modify their behavior. Then it retrieves images from them and displays them in the viewport.

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

Then there's also Lookup Table Calculator but it could really be a separate application altogether, it isn't really tied to anything and nothing is really tied to it. So I'll go over it in the [Energy Compensation](./EnergyCompensation.md) section.