# Long compressed audio segments

Version 0.18.6 raises the NGS2 compressed-segment limit and per-voice compressed
budget from 16 MiB to 32 MiB. The total compressed budget stays at 128 MiB;
the queue remains limited to eight events. PCM buffers, gain, playback rate,
codecs and guest ABI are unchanged.

Some long dialogue banks exceed the former segment ceiling. A session with
reported missing dialogue showed capacity rejections at the matching scene
transitions. This supports the size-limit explanation, although the detailed
trace did not identify the exact guest requests rejected in that session.

The focused synthetic test admits 16 MiB, 17,426,304 bytes, 20,298,432 bytes
and 32 MiB. It verifies rejection above 32 MiB, the eight-event queue,
per-voice accumulation, the unchanged 128 MiB global ceiling, failed copies
and byte release on stop, reconfiguration and destruction. It exercises the
real admission/accounting code with eight-channel metadata and synthetic
bytes; it does not decode or publish game audio.

These admission tests do not certify audible dialogue in every cutscene.
This release does not include a rendering/frame-order fix.
