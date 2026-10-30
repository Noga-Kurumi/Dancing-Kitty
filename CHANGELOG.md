# Changelog

## 1.1.0

- Start local tempo learning alongside catalog requests, with bounded provider timeouts.
- Reuse collected rhythm observations when local/catalog BPM becomes available, preserving the same confidence requirements.
- Add an eight-frame gray dotted loading indicator on the forehead while waiting for BPM.
- Play at a standard 25 fps, hold frame 10 until the next beat, and accelerate only for short beat intervals. Remove slow-song halfway loops.
- Reinforce the topmost position on foreground-window changes without stealing focus or adding polling.

## 1.0.0

First public Windows x64 release.

- Native, always-on-top transparent cat with ten rhythm-synchronized frames.
- Event-driven music classification, public BPM lookup and offline local tempo fallback.
- Sliding-window phase correction and continuously refined local tempo.
- Finish the current loop and hold frame 10 when music stops.
- Dragging, invisible corner resizing and persistent position/size.
- System tray controls, optional login startup and sixteen menu translations.
- Frame-10 executable/tray icon, single-file launcher and portable distribution.
