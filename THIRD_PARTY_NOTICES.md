# Third-party components

- **BeatNet** — Mojtaba Heydari and contributors. [Upstream](https://github.com/mjhydri/BeatNet), CC BY 4.0. The pretrained network was exported to ONNX with explicit recurrent state; see `models/README.md`. License: `licenses/LICENSE-BeatNet.txt`.
- **YAMNet** — Google / TensorFlow Models contributors. [Upstream](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet), Apache-2.0. The bundled inference graph is in ONNX format. License: `licenses/LICENSE-YAMNet.txt`.
- **ONNX Runtime 1.23.2** — Microsoft and contributors. [Upstream](https://github.com/microsoft/onnxruntime), MIT. Unmodified native runtime binaries are included in the downloadable Windows packages. License and notices: `licenses/LICENSE-ONNX-Runtime.txt`, `licenses/NOTICES-ONNX-Runtime.txt`.
- **Microsoft Visual C++ runtime** — Microsoft. The Windows packages include the required, unmodified x64 runtime DLLs from the Visual Studio redistributable set. These remain subject to Microsoft's terms; see [the Visual Studio redistributable code list](https://learn.microsoft.com/visualstudio/releases/2022/redistribution).

The ten PNG animation frames are the assets supplied for Dancing Kitty. The executable icon is derived from frame 10. This repository does not specify an additional general license for its original application code or animation assets; the component licenses above apply to their respective material.
