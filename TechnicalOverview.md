# Overview

The main goal for this project was to create energy conserving offline renderer with global illumination with variety of flexible and easy to use materials using Vulkan. I think that the goal has been achieved pretty well. You have 8 material parameters available: Albedo, Emissive, Roughness, Metallic, Specular Strength, Specular Tint, Transparency, IOR, that can be easily tweaked to create whatever combination you like the most. Everything is powered by Vulkan and all of the calculations are done on the GPU, so the entire thing runs pretty fast, especially compared to the CPU path tracers. All of the renders that you can find in the gallery are either 1000x1000 or 2000x2000 and haven't taken more than 5-10 minutes to render (except for the sponza one) (all of them were rendered using RTX 3060) (More info in the [Benchmark](#benchmark) section) and their quality is I'd say really good, especially after denoising. So let's take a deeper look how exactly is everything working.

# Table Of Contents

- [Overview](#overview)
- [Table Of Contents](#table-of-contents)
- [Implementation Details](#implementation-details)
  - [Ray Tracing](#ray-tracing)
    - [RT Pipeline](#rt-pipeline)
    - [Shader types](#shader-types)
      - [Raygen Shader](#raygen-shader)
        - [Ray Generation](#ray-generation)
        - [Anti Aliasing](#anti-aliasing)
        - [Depth Of Field](#depth-of-field)
        - [Russian Roulette](#russian-roulette)
        - [Fireflies elimination](#fireflies-elimination)
      - [Closest Hit Shader](#closest-hit-shader)
      - [Miss Shader](#miss-shader)
  - [BSDF And Light Transport](#bsdf-and-light-transport)
    - [Light Transport](#light-transport)
    - [BSDF](#bsdf)
      - [Diffuse](#diffuse)
      - [Dielectric](#dielectric)
      - [Metallic](#metallic)
      - [Glass](#glass)
    - [Energy Conservation](#energy-conservation)
      - [Rendering Equation](#rendering-equation)
      - [Final Result](#final-result)
      - [My Implementation](#my-implementation)
  - [Denoising](#denoising)
- [Architecture](#architecture)
- [Benchmark](#benchmark)

    

# Implementation Details

## Ray Tracing

First, let's look into how exactly is the path tracing working.

### RT Pipeline

The entire ray traversal through the scene is handled by a Vulkan Ray Tracing Pipeline and not my custom code. Why? It's faster, because unlike doing everything in compute shaders, it enables you to use specialized RT cores on modern GPUs. And It's way simpler, You don't have to code your own BVH, ray geometry intersections etc. all of that is handled for you, so it is generally faster to deploy.

### Shader types
The Vulkan Ray Tracing Pipeline is fairly flexible because it's made out of several programmable shader stages. These shader stages are designed to handle different aspects of ray traversal and interaction with the scene. There are 5 types of these shaders but I'm using only 3 of them:

**Ray Generation Shader**
* It serves as an entry point for the ray tracing by generating the rays. It basically defines origin and direction of rays.

**Closest-Hit Shader**
* This shader is called for each ray intersection with the geometry. It handles the actual shading of the hit point.

**Miss Shader**
* It's called when there are no intersections in the scene.

#### Raygen Shader

First let's talk about ray gen shader. There are 2 approaches for generating rays - Recursive and loop based.

##### Ray Generation
* In recursive approach you generate only 1 ray per pixel in raygen shader, shoot it into the scene, and if it hits something the closest hit shader takes over. Then you spawn another ray but this time you do it from hit shader and not raygen. That's why it's called recursive, you invoke hit shader from hit shader.
* In loop based approach on the other hand, you create a loop inside the ray gen shader which spawns rays over and over again and don't use recursion in closest hit shader. You just get back into the raygen and spawn another ray when the last one left off.

I used the loop based approach as it is way better than a recursive one, why?
* I found out that for whatever reason it's about 2-3 times faster depending on scene than the recursive method. It's not only my machine as other guides on RT pipeline I saw noticed that as well.
* You're not constricted by the depth limit. In RT pipeline you can't just use recursion infinitely, I don't know what's the minimum guaranteed limit but on my computer it's 31, it of course varies per device. You can query the limit using VkPhysicalDeviceRayTracingPipelinePropertiesKHR.maxRayRecursionDepth. If you cross the limit device will be lost so you'll most likely just crash. Of course loop base approach doesn't have this limit, you can bounce rays through the scene for as long as you like.

Now let's talk about some interesting techniques that I used in my raygen shader: Anti aliasing, Russian Roulette, Depth of field, and fireflies elimintaion.

##### Anti Aliasing

Aliasing is a known artifacts in computer graphics, it's caused by the fact that in real world cameras edges of pixels are a blend of foreground and background, that's because in real world the space is continuous, it has infinite resolution, so if we want to turn this space into pixels we're basically averaging all the space that pixel takes up. In computer graphics we're not averaging anything, we're just shooting a ray in some direction and sampling it's color. This approach is called **point sampling**, luckily for us in path tracing we're averaging multiple rays per pixel anyway, so the easiest fix to our problem is just offseting the rays a little bit in random direction so that they are average color of the space that the pixel takes up.

It's really simple in terms of code, we just generate random point on a 2D square and then we offset the ray origin and ray direction with it. Here's the result:

<p align="center">
  <img src="./Gallery/materialShowcase/AntiAliasingOn.png" alt="Furnace Cornell" width="520" height="200" />
  <img src="./Gallery/materialShowcase/AntiAliasingOff.png" alt="Furnace Cornell" width="520" height="200" />
</p>

<p align="center"> 
<b>Left image was rendered with antialiasing and the right was rendered without. The difference is quite clear.
</b>
</p>

##### Depth Of Field
DOF TODO

##### Russian Roulette

The entire path tracer is basically one big monte carlo simulation

$$F = \frac{1}{N} \sum_{i=1}^{N} F_i$$

where $F$ is the integrand (light contribution of the ray), $F_i$ are random samples (different directions sampled), and $N$ is the number of samples.

To speed up the rendering we can use a method called **Russian Roulette** which terminates path with low throughput without biasing the result (keeping the expected value the same). Here's how it works:

Theoritically to have a completely unbiased and mathematically correct result of the sample we should bounce the ray through the scene infinite amount of times, which is completely impossible to do. But as each succesive bounce does less and less visually (it's throughput decreases) path tracers usually set the bounce limit to some hardcoded value like 10 or 20, so the image is still "visually" correct, although it's mathematically wrong. But how do we choose this limit? If it's too small we're biasing the result, if it's too large we're wasting time on computing low throughput paths which don't change anything visually. Here's when the **Russian Roulette** comes into play.

At each ray-surface intersection, we set a probability $p_i$ it can be chosen in any manner, I set it based on the maximum value of one of three RGB ray channels. A random number $r$ is generated, and if $r$ is less than $p_i$, the ray continues, otherwise, it terminates. If the ray continues, its contribution is multiplied by a factor of $\frac{1}{p_i}$ to account for the termination of other paths. Here's a mathematical formulation of this:

When applying Russian Roulette, we replace each term $F_i$ with $F_i\prime$ where:

$$ F_i\prime = \begin{cases} 
0 & \text{with probability } (1 - p_i) \\
\frac{F_i}{p_i} & \text{with probability } p_i 
\end{cases} $$

We can see that as long as we divide the $F_i$ by $p_i$ the expected value remains mathematically unbiased:

$$ E[F_i\prime] = \left(1 - p_i\right) \cdot 0 + p_i \cdot \frac{E[F_i]}{p_i} = E[F_i] $$

And because we're terminating only paths with low throughput we're not introducing too much variance and keeping the convergence rate relatively similiar to the original one. This way the result is not only mathematically correct, but we're also saving a lot of computation time by not evaluating low throughput paths. 

By applying russian roulette we're in fact **always** introducing more variance, but if probability $p$ is chosen correctly we're gaining efficiency which outweights small variance that it introduces. But if for some reason the probability $p$ is chosen poorly, like a constant of 0.01. We'd only trace 1% of the camera rays and then multiply them by a 100. From a mathematical point of view, the image is still correct, it will eventually converge on the right result. But visually it's horrible, the image will be way darker as the probabiity of a path having 20 bounces is close to zero as each bounce's probability of being terminated is 99%.

<p align="center">
  <img src="./Gallery/materialShowcase/NoRoulette366s.png" alt="No Roulette" width="500" height="500" />
  <img src="./Gallery/materialShowcase/Roulette150s.png" alt="Roulette" width="500" height="500" />
  <img src="./Gallery/materialShowcase/IncorrectRoulette55s.png" alt="Incorrect Roulette" width="500" height="500" />
</p>

<p style="text-align:center;"> 
Left image was rendered without russian roulette (366s for 200k samples per pixel)
</p>
<p style="text-align:center;"> 
Middle image was rendered with roulette with correctly picked probability (150s for 200k samples per pixel)
</p>
<p style="text-align:center;"> 
right image was rendered with roulette using constant probability 0.1 (55s for 200k samples per pixel)
</p>

So in general, this method reduces computation by terminating a lot of the low contributing paths while keeping the rendering unbiased. It gives you a nice performance boost based on the scene settings, more on that in the [Benchmark](#benchmark) section.

##### Fireflies elimination

Fireflies elimination is a method used to limit the luminance of the sampled ray. Because of lack of complex light transport algorithm if we pick environment map with a few very bright spots the variance goes through the roof. And unfortunately there's not much we can do about that, so the only solution is to limit the luminance of the rays that hit those very bright spots.

If I pick a 2k env map that has 3 pixels that are really bright and I just path trace with my naive approach it will be a disaster, I would probably have to run this for several days to bring down the noise to a level where I can denoise it. Otherwise the variance is just too big to even be denoised (at least with my denoiser).

<p align="center">
  <img src="./Gallery/materialShowcase/Fireflies.png" alt="No Roulette" width="600" height="600" />
  <img src="./Gallery/materialShowcase/FirefliesDenoise.png" alt="Roulette" width="600" height="600" />
</p>

<p align="center"> 
The left image shows cornell box lit by a very bright env map, the image has 200k samples per pixel. Right image shows attept at denoising it.
</p>

So the only real solution to this problem (except for using better light transport algorithm) is just limiting the luminance of the environment map to limit variance. Here's an image with luminance limited to 500:

<p align="center">
  <img src="./Gallery/materialShowcase/FirefliesEliminated.png" alt="No Roulette" width="600" height="600" />
</p>

<p align="center"> 
Image Shows the same image as above (200k samples per pixel) but this time max luminance of the env map is limited to 500.
</p>

As you can see the image is way darker, but that's logical if we're literally limiting brightness. Unfortunately that's really the only way of keeping the noise on "stable" and denoisable level for some environment maps.

#### Closest Hit Shader

The closest hit is pretty simple, it queries mesh data, computes barycentric coordinates of the hit point, calculates material properties and the shading itself is done by the BSDF which I'll talk about later.

#### Miss Shader

The miss shader is the simplest one, there's really nothing to talk about here, it just converts ray direction to texture coordinates and returns color of the environment map texture.

## BSDF And Light Transport

Light transport and BSDF are the most important parts of the path tracer. BSDF determines shading of the pixels and properly implemented light tranport algorithm determines how fast the path tracer will converge on the correct result. So maybe let's talk about specific implementation of these two.

### Light Transport

For light transport I'm using simple naive approach of picking ray directions based on the BSDF, so for mirror-like surface the direction will be perfect reflection and for more matte surfaces it will be random direction on the hemishpere. This approach results in insane amount of noise in large scenes with small lights, but that's okay for me, the goal of this project was to create energy conserving global illumination and not the fastest path tracer in the world. So using a complex light transport algorithm was way out of the scope for this project. Maybe some day I'll make it bidirectional or add something like MIS.

### BSDF

Current BSDF consists of 4 lobes
* Diffuse
* Dielectric
* Metallic
* Glass

#### Diffuse
Diffuse lobe is pretty simple. As color it just returns the color of the surface. The ray direction is a random direction with cosine weighted distribution, meaning that directions will more likely be sampled where the $\cos(\phi)$ is bigger. $\phi$ being the angle between the reflected ray direction and the surface normal.

#### Dielectric
Dielectric lobe chooses color based on **SpecularTint** factor, the bigger the **SpecularTint** the more color will be shifted from 1 to the material color. The ray direction is choosen based on the GGX distribution. It returns half vector in which microfacet is pointing. The bigger the **Roughness** factor the bigger the chance that it will be pointing in direction different from surface normal.

#### Metallic
Metallic lobe returns the material color same as the diffuse lobe. For the ray direction it uses the exact same distribution as dielectric lobe.

#### Glass
Glass lobe can have 2 cases - reflection and refraction. Whether the light will reflect or refract is based on Fresnel term ([more info here](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission#FrDielectric)). Same as before we calculate the half vector, if the ray refracts we choose material color as output color and if it reflects we choose 1 (we choose one because the glass is dielectric so we don't want to tint it).

Lobes are sampled semi randomly, each lobes has it's weight, the bigger the weight the bigger the probability that it will be sampled. All of the weights calculation can be found in the code. You can find the code in the [BSDF.glsl](https://github.com/Zydak/Vulkan-Path-Tracer/blob/main/PathTracer/src/shaders/BSDF.glsl). Also, all of the lobes described above are energy conserving, as that was the main goal for this project, to make a energy conserving path tracer, but what does that even mean and how did I check that?

### Energy Conservation
In short, if we say that something is energy conserving we mean that light is neither created nor destroyed as it bounces around a scene. We can check for energy conservation using a furnace test. What is a furnace test? The general idea is that if you have a 100% reflective object that is lit by a uniform environment, it becomes indistinguishable from the environment. It doesn’t matter if the object is matte or mirror like, or anything in between, it just “disappears”. We can easily prove that:

#### Rendering Equation
Color of a pixel is given by the outgoing radiance $L_o$ in the direction $\omega_o$ which is given by solving a rendering equation. Here I'll use a simple one where BRDF = $\frac{albedo}{\pi}$. But this easily extends to more complex BRDFs, BSDFs etc.

$$
L_o(\mathbf{x}, \omega_o) = \int_{\Omega} L_i(\mathbf{x}, \omega_i) \cdot \rho(\mathbf{x}, \omega_i) \cdot \cos(\theta_i) d\omega_i
$$

where:
- $L_o(\mathbf{x}, \omega_o)$ is the outgoing radiance in the direction $\omega_o$ from the point $\mathbf{x}$,
- $L_i(\mathbf{x}, \omega_i)$ is the incoming radiance from the direction $\omega_i$,
- $\rho(\mathbf{x}, \omega_i)$ is the reflectance or BRDF / BSDF at point $\mathbf{x}$,
- $\cos(\theta_i)$ is the cosine of the angle $\theta_i$ between the surface normal and the incoming direction $\omega_i$,
- $d\omega_i$ is the differential solid angle in the direction $\omega_i$.

Now if we make our object perfectly reflective (it doesn't absorb any light), so set the albedo factor to 1. Our BRDF should always equal $\frac{1}{\pi}$ ($\rho(\mathbf{x}, \omega_i) = \frac{1}{\pi}$) no matter how rough or metallic it is. Then the rendering equation simplifies to:

$$
L_o(\mathbf{x}, \omega_o) = \frac{1}{\pi} \int_{\Omega} L_i(\mathbf{x}, \omega_i) \cdot \cos(\theta_i) d\omega_i
$$

It's a common knowledge that integral $\int_{\Omega} \cos(\theta_i) d\omega_i$ is always equal $\pi$, that's why you usually don't see calculation of it in code, but let's solve that anyway. To solve it, we use spherical coordinates where:

$$
d\omega_i = \sin(\theta_i) d\theta_i d\phi_i
$$

So the integral becomes:

$$
\int_{\Omega} \cos(\theta_i) d\omega_i = \int_{0}^{2\pi} \int_{0}^{\pi/2} \cos(\theta_i) \sin(\theta_i) d\theta_i d\phi_i
$$

Separate the integrals:

- **Azimuthal Integral**:

$$
\int_{0}^{2\pi} d\phi_i = 2\pi
$$

- **Polar Integral**:

Substitute $u = \sin(\theta_i)$ so $du = \cos(\theta_i) \ d\theta_i$:

$$
\int_{0}^{\pi/2} \cos(\theta_i) \sin(\theta_i) d\theta_i = \int_{0}^{1} u du = \frac{u^2}{2} \bigg|_{0}^{1} = \frac{1}{2}
$$

Combining the results:

$$
\int_{\Omega} \cos(\theta_i) d\omega_i = 2\pi \cdot \frac{1}{2} = \pi
$$

#### Final Result

Substitute this result back into the rendering equation:

$$
L_o(\mathbf{x}, \omega_o) = \frac{1}{\pi} \cdot \pi \cdot L_i(\mathbf{x}, \omega_i) = L_i(\mathbf{x}, \omega_i)
$$

As you can see, $L_o(\mathbf{x}, \omega_o) = L_i(\mathbf{x}, \omega_i)$. The $L_i(\mathbf{x}, \omega_i)$ is equal to the background color, and because the background is uniform, it doesn't matter in which direction the ray is reflected, so it doesn't matter whether the surface is a perfect mirror or very rough, the ray direction doesn't matter as every direction will return the same value. This means that $L_o(\mathbf{x}, \omega_o)$ will always equal the background color for every pixel. And that means that we won't be able to distinguish between an object and the background! 

So if I use a perfectly white background and place a perfectly reflective sphere in the scene, I shouldn't be able to see it. And that is exactly what happens!
<p align="center">
  <img src="./Gallery/materialShowcase/PassedFurnace.png" alt="Passed Furnace" width="500" height="500" />
</p>

Why is any of this useful? Well, if we mess up any part of the renderer, like the BSDF the furnace test will fail. If we absorb too much energy (destroy light), we'll see a dark sphere on a white background. On the other hand, if we reflect too much energy (create light), we'll see a sphere brighter than the background. And that's exactly what happens if I intentionally break my BSDF:

Here's the result of incorrectly calculating dielectric reflection color:
<p align="center">
  <img src="./Gallery/materialShowcase/FailedFurnace.png" alt="Failed Furnace" width="500" height="500" />
  <img src="./Gallery/materialShowcase/FailedFurnace1.png" alt="Failed Furnace1" width="500" height="500" />
</p>

You can clearly see that something is wrong. On the image on the left the BSDF is absorbing too much energy at high angles. On the image on the right the BSDF is reflecting too much energy at high angles. In both cases $L_o(\mathbf{x}, \omega_o) \neq L_i(\mathbf{x}, \omega_i)$ which means that some part of the rendering equation is messed up, in this case it's BSDF which no longer equals $\frac{1}{\pi}$.

#### My Implementation

Now, back to my implementation, does it properly conserve energy? Yes.

As a proof, you can see the furnace test done on the cornell box that you can find in a gallery, but this time all materials have albedo equal 1 (perfect reflector) with roughness and metallic factors also set to 1. You can try any model with any material settings yourself and it won't be visible.
<p align="center">
  <img src="./Gallery/materialShowcase/CornellFurnace.png" alt="Furnace Cornell" width="500" height="500" />
</p>

You can't see it but that's the point, I promise it's there!

Here's how it looks with a non-uniform environment map:
<p align="center">
  <img src="./Gallery/materialShowcase/CornellFurnace1.png" alt="Furnace Cornell1" width="500" height="500" />
</p>


## Denoising
TODO

# Architecture

TODO


# Benchmark

TODO