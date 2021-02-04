# ivl-cr
A cinematic renderer for dicom data

Supports hybrid scattering with either:
 * lambertian diffuse + disney clearcoat if surface, or
 * schlick approximation of Henyey-Greenstein volume phase function

 Uses voxel cone tracing to approximate calculation of diffuse and volumetric shading

![sample](renders/hero.png)

## To Build
Install [vcpkg](https://github.com/microsoft/vcpkg)

The installation instructions are on the readme but its pretty simple,  just clone the repo to a permanent location and run `bootstrap-vcpkg.bat` to set it up and run this to integrate with visual studio: ` .\vcpkg.exe integrate install`. I use powershell but I think it might work with other shells too.

Then install the project dependencies: <br>
`.\vcpkg.exe install opengl glew glfw3 glm` <br>
`.\vcpkg.exe install dcmtk winsock2 yaml-cpp` <br>

Note that vcpkg installs the 32 bit versions of these by default, I tried using 64 bit for everything but for some reason dcmtk was giving me linker errors so I stuck to 32 bit.

You might also need the Windows SDK installed to fix some linker errors with dcmtk, if you have the Visual Studio IDE open the Visual Studio Installer and modify your installation to include it. Then go to project properties > Linker > General and add `C:\Program Files (x86)\Windows Kits\10\Lib\<sdk version number>\um\x86` to Additional Library Directories and then Linker > Input and add `iphlpapi.lib;NetAPI32.lib;` to Additional Dependencies.

The directories above were the only things specific to my setup in the .sln file in my repo (i think). It should work after these are changed. Just make sure you have some scans in the scans/ folder (which I have `.gitignore`-d), the usage is: <br>
`.\Debug\ivl-cr.exe [scan [config]]`

Where scan is the name of the folder that contains the scan in the `scans/` folder and config is the name of the config yaml file in the `configs/` folder. Make sure that the working directory you call the exe from is the same directory that contains the scans and configs folders.