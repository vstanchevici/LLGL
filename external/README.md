External dependencies to build LLGL
-----------------------------------

* **GaussianLib** contains the *optional* submodule with header files for the examples; Only required if `LLGL_BUILD_EXAMPLES` is enabled.
* **OpenGL** contains the extended OpenGL header files to include `<GL/glext.h>` and `<GL/wglext.h>`; Only required if `LLGL_GL_INCLUDE_EXTERNAL` is enabled.
* **SPIRV-Headers** contains the *optional* submodule to include `<spirv/1.2/spirv.hpp11>`; Only required if `LLGL_VK_ENABLE_SPIRV_REFLECT` is enabled.
  *NOTE*: It is highly recommended to enable this option. Otherwise, LLGL will not be able to create shader permutations which is necessary when PSO layouts contain both dynamic and heap resources.
* **stb** contains the public-domain header files `stb_image.h` and `stb_image_write.h` (source: https://github.com/nothings/stb). Only used for examples.
