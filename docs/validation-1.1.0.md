# Validation of 1.1.0

Six C++ regression tests passed, covering audio transport/music gating, recording matching, phase/tempo tracking, provider response validation, local tempo and animation/loading behavior. New checks exercise warm phase acquisition, retention of observations on tempo handoff, expiry of old evidence, deadline exhaustion, constant-speed/accelerated strokes, idle holds and reversible spinner compositing at several sizes.

## Recorded-audio replay

The same complete local recordings of **No Existes** and **Prófugos** were replayed through the native 1.0.0 and 1.1.0 analysis probes, using the same ONNX graphs. Catalog access was disabled and the music gate was assumed positive. The probes use the audio timeline rather than elapsed processing time. No music recordings are included in this repository or in the release.

| Recording | First local BPM, both versions | First confirmed pulse, 1.0.0 | First confirmed pulse, 1.1.0 | Final local BPM, both versions |
| --- | ---: | ---: | ---: | ---: |
| No Existes | 16.10 s | 19.77 s | 16.77 s | 80.238 |
| Prófugos | 13.72 s | 16.05 s | 14.16 s | 127.041 |

The local estimator's acceptance threshold and initial BPM timing were unchanged in this comparison. The improvement comes from retaining the rhythm observations already collected when BPM becomes ready, avoiding another phase-learning interval. Counts of confirmed pulses were 347 → 351 and 644 → 648 respectively. These timings are specific to these recordings and their intros, not a universal startup guarantee.

In the live engine, local estimation now starts alongside catalog lookup, so it also avoids waiting for unsuccessful/slow online providers. The offline replay above does not measure that network-related improvement. Each provider has a total deadline shared by headers and all body reads: 3 seconds for Deezer, then 4.5 seconds for MusicBrainz/AcousticBrainz, including provider rate-limit waits. Local estimation continues during those requests. A valid later catalog result can replace the local tempo using the existing rhythmic evidence.

## Visual and window checks

- Eight native gray-dot loading poses were rendered over frame 10 and inspected. Sprite pixels are restored after compositing; the original PNG files are unchanged.
- Synthetic animation tests cover slow songs holding frame 10, faster songs fitting the stroke into their interval, phase refinements, pause/resume and 5,000 fractional-period cycles without accumulated timing drift.
- An isolated native window fixture removed its topmost flag, then received a foreground event from a separate fixture process. The hook restored topmost status; the user's foreground window remained unchanged. No periodic focus polling was used.

The foreground-event fix applies to ordinary windows on the current desktop. Windows-controlled secure desktops and exclusive rendering modes are outside this test. There was no new human annotation of the recordings or universal accuracy evaluation in this update.
