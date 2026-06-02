# NvCloth 1.1.6

This directory contains the engine-facing NvCloth ThirdParty package.

## Contents

```text
include/
  NvCloth/      Public NvCloth headers
  NvClothExt/   Public NvCloth extension cooker headers
lib/
  win.x86_64.vc141.cuda10.md/
    debug/      NvClothDEBUG_x64.lib
    release/    NvCloth_x64.lib
bin/
  win.x86_64.vc141.cuda10.md/
    debug/      NvClothDEBUG_x64.dll
    release/    NvCloth_x64.dll
license.txt
```

The current binaries were rebuilt with CUDA Toolkit 10.0 and the VS2017 v141 toolset, with a `compute_60` PTX fallback embedded for newer NVIDIA drivers/GPUs. See `Docs/NvCloth_IntegrationPlan.md` for the exact rebuild and verification notes.

Samples, DXUT, DirectXTex, sample renderer code, generated sample solutions, and the upstream `.git` directory are intentionally not included.

## Source Build Workspace

The current source build workspace used to produce these binaries is:

```text
C:\Users\jungle\Desktop\YG\Week13\NvCloth
```

The engine should use this directory only through public headers and prebuilt binaries. Runtime integration should go through engine wrappers such as `FNvClothContext` and `FClothInstance`.
