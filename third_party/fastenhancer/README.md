# FastEnhancer（48 kHz）

- 來源：aask1357/fastenhancer，Release `onnx-48khz-v1` 的 `fastenhancer_{t,b,s,m}.onnx`（MIT）；L 刻意不收
- 串流格式：`wav_in [1,hop]`、`cache_in_*` → `wav_out [1,hop]`、`cache_out_*`
- T、B、S：hop 512，固有延遲 512 樣本；M：hop 320，固有延遲 704 樣本（皆為 n_fft 1024 − hop，等於 `cache_in_0` 的長度）
