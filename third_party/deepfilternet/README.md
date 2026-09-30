# DeepFilterNet 3（C 介面）

- 來源：Rikorose/DeepFilterNet v0.5.6，`libDF`（MIT／Apache-2.0 雙授權，本專案採 MIT）
- `DeepFilterNet3_onnx.tar.gz`：官方 DFN3 模型，執行時以路徑傳給 `df_create`
- `bin/df.dll`、`lib/df.dll.lib`：自行編譯的 C 介面

## 重新編譯

```
git clone --depth 1 --branch v0.5.6 --filter=blob:none --sparse https://github.com/Rikorose/DeepFilterNet.git
cd DeepFilterNet
git sparse-checkout set --no-cone /libDF/ /models/DeepFilterNet3_onnx.tar.gz /Cargo.toml /Cargo.lock "/LICENSE*" /cbindgen.toml
# 工作區 Cargo.toml 的 members 只留 "libDF"
cargo update -p time        # time 0.3.28 在新版 Rust 編不過，升到 0.3.36+
cargo rustc --release -p deep_filter --lib --crate-type cdylib --no-default-features --features capi
```

輸出在 `target/release/df.dll` 與 `df.dll.lib`。
