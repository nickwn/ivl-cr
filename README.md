# ivl-cr
A cinematic renderer for dicom data

Supports hybrid scattering with either:
 * lambertian diffuse + disney clearcoat if surface, or
 * schlick approximation of Henyey-Greenstein volume phase function

 Uses voxel cone tracing to approximate calculation of diffuse and volumetric shading

![sample](renders/hero.png)