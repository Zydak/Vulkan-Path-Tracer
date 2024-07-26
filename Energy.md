# Energy Conservation
In short, if we say that something is energy conserving we mean that light is neither created nor destroyed as it bounces around a scene. We can check for energy conservation using a furnace test on different materials. What is a furnace test? The general idea is that if you have a 100% reflective object that is lit by a uniform environment, it becomes indistinguishable from the environment. It doesn’t matter if the object is matte or mirror like, or anything in between, it just “disappears”. You can easily prove that:

Color of a pixel is given by the outgoing radiance $L_o$ in the direction $\omega_o$ which is given by solving a rendering equation:

$$
L_o(\mathbf{x}, \omega_o) = \int_{\Omega} L_i(\mathbf{x}, \omega_i) \cdot \rho(\mathbf{x}, \omega_i) \cdot \cos(\theta_i) \, d\omega_i
$$

where:
- $L_o(\mathbf{x}, \omega_o)$ is the outgoing radiance in the direction $\omega_o$ from the point $\mathbf{x}$,
- $L_i(\mathbf{x}, \omega_i)$ is the incoming radiance from the direction $\omega_i$,
- $\rho(\mathbf{x}, \omega_i)$ is the reflectance or BRDF / BSDF at point $\mathbf{x}$,
- $\cos(\theta_i)$ is the cosine of the angle $\theta_i$ between the surface normal and the incoming direction $\omega_i$,
- $d\omega_i$ is the differential solid angle in the direction $\omega_i$.

Now if we make our object perfectly reflective (it doesn't absorb any light), so set the albedo factor to 1. Our BRDF should always equal 1 ($\rho(\mathbf{x}, \omega_i) = 1$) no matter how rough or metallic it is. Then the rendering equation simplifies to:

$$
L_o(\mathbf{x}, \omega_o) = \frac{1}{\pi} \int_{\Omega} L_i(\mathbf{x}, \omega_i) \cdot \cos(\theta_i) \, d\omega_i
$$

It's a common knowledge that integral $\int_{\Omega} \cos(\theta_i) \, d\omega_i$ is always equal $\pi$, that's why you usually don't see calculation of it in code, but let's solve that anyway. To solve it, we use spherical coordinates where:

$$
d\omega_i = \sin(\theta_i) \, d\theta_i \, d\phi_i
$$

So the integral becomes:

$$
\int_{\Omega} \cos(\theta_i) \, d\omega_i = \int_{0}^{2\pi} \int_{0}^{\pi/2} \cos(\theta_i) \sin(\theta_i) \, d\theta_i \, d\phi_i
$$

Separate the integrals:

- **Azimuthal Integral**:

$$
\int_{0}^{2\pi} d\phi_i = 2\pi
$$

- **Polar Integral**:

Substitute $u = \sin(\theta_i)$ so $du = \cos(\theta_i) \ d\theta_i$:

$$
\int_{0}^{\pi/2} \cos(\theta_i) \sin(\theta_i) \, d\theta_i = \int_{0}^{1} u \, du = \frac{u^2}{2} \bigg|_{0}^{1} = \frac{1}{2}
$$

Combining the results:

$$
\int_{\Omega} \cos(\theta_i) \, d\omega_i = 2\pi \cdot \frac{1}{2} = \pi
$$

### Final Result

Substitute this result back into the rendering equation:

$$
L_o(\mathbf{x}, \omega_o) = \frac{1}{\pi} \cdot \pi \cdot L_i(\mathbf{x}, \omega_i) = L_i(\mathbf{x}, \omega_i)
$$

As you can see, $L_o(\mathbf{x}, \omega_o) = L_i(\mathbf{x}, \omega_i)$. That means that if we choose a uniform environment light, the $L_i(\mathbf{x}, \omega_i)$ will equal to the background color. And that means that we won't be able to distinguish between an object and the background. So if I use a perfectly white background and place a perfectly reflective sphere in the scene, I shouldn't be able to see it.

And that is exactly what happens!
<p align="center">
  <img src="./Gallery/materialShowcase/PassedFurnace.png" alt="Passed Furnace" width="500" height="500" />
</p>

Why is any of this useful? Well, if we mess up any part of the renderer, like the BSDF the furnace test will fail. If we absorb too much energy (destroy light), we'll see a dark sphere on a white background. On the other hand, if we reflect too much energy (create light), we'll see a sphere brighter than the background. And that's exactly what happens if I intentionally break my BSDF:

Here's the result of incorrectly calculating dielectric reflection color:
<p align="center">
  <img src="./Gallery/materialShowcase/FailedFurnace.png" alt="Failed Furnace" width="500" height="500" />
</p>
You can clearly see that something is wrong. The BRDF is absorbing too much energy at high angles.

# My Implementation

Now, back to my implementation, does it properly conserve energy? Yes.

As a proof, you can see the furnace test on the cornell box that you can find in a gallery, but this time all materials have albedo equal 1 (perfect reflector) with roughness and metallic factors also set to 1.
<p align="center">
  <img src="./Gallery/materialShowcase/CornellFurnace.png" alt="Furnace Cornell" width="500" height="500" />
</p>

You can't see it but that's the point, I promise it's there!

Here's how it looks with a non-uniform environment map:
<p align="center">
  <img src="./Gallery/materialShowcase/CornellFurnace1.png" alt="Furnace Cornell1" width="500" height="500" />
</p>

