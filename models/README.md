# Audio models

The checked-in graphs are the exact two models used for the native application. File sizes and SHA-256 digests are in `manifest.json`.

| File | Purpose | Upstream | License |
| --- | --- | --- | --- |
| `yamnet.onnx` | Audio classification / music gate | [YAMNet, TensorFlow Models / Google](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet) | Apache-2.0 |
| `beatnet.onnx` | Stateful rhythmic observations | [BeatNet, Mojtaba Heydari and contributors](https://github.com/mjhydri/BeatNet) | CC BY 4.0 |

These are ONNX-format versions of the upstream networks; the graph format/export differs from the original model distribution. The BeatNet graph exports recurrent hidden/cell state for single-frame streaming, using the first pretrained model. Dancing Kitty supplies its own native input frontend and rhythm scheduling; it does not ship the Python inference application or its particle filter.

YAMNet: Dan Ellis and the TensorFlow Models contributors, *YAMNet: a pretrained audio event classifier*.

BeatNet: Mojtaba Heydari, Frank Cwitkowitz and Zhiyao Duan, *BeatNet: CRNN and Particle Filtering for Online Joint Beat Downbeat and Meter Tracking*, ISMIR 2021. See [the upstream project](https://github.com/mjhydri/BeatNet) for the original work.

The license texts are in `../licenses/`. No training or test music recordings are distributed.
