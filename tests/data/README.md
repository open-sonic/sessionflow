# Test data

0.1 s stereo sine at 441 Hz and 44.1 kHz, left at 0.5, right at 0.25. Generated with ffmpeg:

```sh
SRC="aevalsrc=0.5*sin(2*PI*441*t)|0.25*sin(2*PI*441*t):s=44100:d=0.1"
ffmpeg -f lavfi -i "$SRC" -map_metadata -1 -fflags +bitexact -c:a flac -sample_fmt s32 -bits_per_raw_sample 24 sine_24bit.flac
ffmpeg -f lavfi -i "$SRC" -map_metadata -1 -fflags +bitexact -c:a libmp3lame -b:a 128k sine.mp3
```

WAV tests write their files at run time.
