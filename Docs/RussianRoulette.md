## Russian roulette

For complex scenes path length became a performance bottleneck. Setting a fixed bounce limit felt arbitrary, too low and I'd lose important indirect lighting, too high and I'd waste performance on negligible contributions. So for determining path length I decided to use *Russian Roulette*. After every bounce of the ray, probability of ray continuing it's path $p$ is set. It can be chosen in any manner. I set it based on the maximum value of one of three RGB channels of surface contribution. A random number $\xi \sim \mathcal{U}(0, 1)$ is then generated and if $\xi$ is greater or equal than $p$, the ray is terminated. If ray continues, contribution is weighted by $p$ to account for the termination of other paths.

If $f$ is contribution of each ray then

$$
f^\prime =
\begin{cases}
0 & \xi \ge p \\
\frac{f}{p} & \text{otherwise}
\end{cases}
$$

The expected value remains the same, so the image will converge to the same result eventually:

$$
E[f^\prime] = (1 - p) \cdot 0 + p \cdot \frac{E[f]}{p} = E[f]
$$

This of course introduces more variance, but as long as probability of ray continuation $p$ is chosen correctly, and rays aren't terminated too often, the performance boost will easily outweigh small variance.

Here's a comparison:

<p align="center">
  <img src="../Gallery/RouletteOff.png" width="45%" />
  <img src="../Gallery/RouletteOn.png" width="45%" />
</p>

Image on the left has russian roulette disabled. It's 2000x2000 pixels, 2.5K samples per pixel (10'000'000'000 samples in total) were taken, bounce limit was set to 20. It took 45s to compute. Image on the right has russian roulette enabled, dimensions and sample count are identical, but this time it took only 20s to compute. It is visually identical to one on the left but the render time has been cut in half. And the performance boost of the russian roulette only increases as the scenes become more complex, and more bounces are needed.

Of course I still had to set maximum bounce limit. Russian roulette gives no guarantee for ray to be terminated if it just keeps bouncing indefinitely between 100% energy conserving surfaces. And there's no place for infinite loops in shaders. So the default limit I use is 200 max bounces, after that, ray is terminated no matter how much energy it has left.
