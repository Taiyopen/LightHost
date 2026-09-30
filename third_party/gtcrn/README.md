# GTCRN（16 kHz 串流版）

- 來源：Xiaobin-Rong/gtcrn，`stream/onnx_models/gtcrn_simple.onnx`（MIT）
- 串流格式：`mix [1,257,1,2]`（一格頻譜：實部、虛部）＋ `conv_cache [2,1,16,16,33]`、`tra_cache [2,3,1,1,16]`、`inter_cache [2,1,33,16]`
- 頻譜轉換：16 kHz、n_fft 512、hop 256、窗為 sqrt(hann)
