## Depth Of Field
All that depth of field effect is trying to achieve is to simulate how real world camera lenses work.

<p align="center">
  <img src="../Gallery/DepthOfField.png"/>
</p>

And since this is a path tracer, light rays are already simulated, all that needs to be done is offsetting the origin slightly and pointing the ray to a focus point.

```
focusPoint = origin + direction * focalLength;
randomOffset = UniformFloat2(-0.5f, 0.5f) * DoFStrenght;
RayOrigin = origin.xyz + cameraRight * randomOffset.x + cameraUp * randomOffset.y;
RayDirection = normalize(focalPoint - origin.xyz);
```

<p align="center">
  <img src="../Gallery/DoFOff.png" width="45%" />
  <img src="../Gallery/DoFOn.png" width="45%" />
</p>