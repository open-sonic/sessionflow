# Test data

Stereo sine at 441 Hz and 44.1 kHz, left at 0.5, right at 0.25. Generated with ffmpeg:

| File | Length | Command |
|---|---|---|
| `sine_24bit.flac` | 0.1 s | `ffmpeg -f lavfi -i "$SRC:d=0.1" -map_metadata -1 -fflags +bitexact -c:a flac -sample_fmt s32 -bits_per_raw_sample 24 sine_24bit.flac` |
| `sine.mp3` | 2 s, long enough for several seek points | `ffmpeg -f lavfi -i "$SRC:d=2" -map_metadata -1 -fflags +bitexact -c:a libmp3lame -b:a 128k sine.mp3` |

With `SRC="aevalsrc=0.5*sin(2*PI*441*t)|0.25*sin(2*PI*441*t):s=44100"`.

WAV tests write their files at run time.
