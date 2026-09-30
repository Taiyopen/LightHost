# CLAUDE.md

Light Host：Windows 系統列常駐的 VST3 外掛宿主，麥克風經「回音消除（WebRTC AEC3）→ 降噪 → 使用者外掛鏈」後輸出。C++17 + JUCE 8.0.4，CMake 建置。

## 架構地圖

| 檔案 | 做什麼 |
|---|---|
| `Source/HostStartup.cpp` | 進入點；建立設定檔、`AppSettings`、`IconMenu` |
| `Source/AppSettings.*` | **設定唯一讀寫入口**：所有鍵名與預設值都在這裡，其他地方不要直接碰 `PropertiesFile` |
| `Source/IconMenu.*` | 系統列圖示與左右鍵選單；擁有 `PluginChain`、`AudioEngine` 與各視窗 |
| `Source/plugins/PluginChain.*` | 外掛清單（掃描到的／掛在鏈上的）、排序、略過；只管資料，不碰音訊圖 |
| `Source/audio/AudioEngine.*` | 音訊裝置、處理圖、外掛狀態存取、AEC 參考訊號擷取；`rebuildGraph()` 重建整張圖 |
| `Source/update/Updater.*` | 自動更新：查 GitHub 最新 Release、下載安裝檔、以 `/SILENT` 執行後讓 App 存檔結束 |
| `Source/audio/Routing.*` | 路由資料：哪幾組輸入送進處理鏈（多組時先相加）、哪些來源（處理後／各輸入原音）送到哪些輸出；以裝置上的聲道組編號存 |
| `Source/audio/LoopbackCapture.*` | 獨立執行緒擷取 WASAPI loopback 當 AEC 參考 |
| `Source/dsp/AecProcessor.*` | AEC3 包裝；參考訊號環形緩衝、重新取樣、幀對齊 |
| `Source/dsp/OutputReferenceTap.*` | 把送往喇叭的訊號餵給 AEC 當參考（App Output 模式） |
| `Source/dsp/NoiseReducerProcessor.*` + `INoiseReducer.h` | 降噪節點（一律在處理鏈上，開關瞬間切換）與可替換的演算法介面 |
| `Source/dsp/noise/FramedNoiseReducer.*` | 模型型降噪的共用外殼：單聲道、取樣率互轉、切 hop、「最多壓幾 dB」（原音對齊延遲後混回） |
| `Source/dsp/noise/ModelNoiseReducers.*` | RNNoise、FastEnhancer、GTCRN、DeepFilterNet 3 各自的一個 hop 處理 |
| `Source/dsp/noise/OnnxStreamingModel.*` | ONNX Runtime 串流模型（固定形狀、預先綁定緩衝、狀態張量依名稱回填） |
| `Source/dsp/noise/NoiseReducerFactory.*` | 設定值 → 演算法；模型檔在執行檔旁的 `models\`；比較表用的頻寬與說明文字 |
| `Source/dsp/noise/NoiseReducerBenchmark.*` | 背景執行緒量測各演算法的延遲與 CPU |
| `Source/ui/NoiseCompareWindow.*` | AEC 分頁「Compare...」開出的比較表 |
| `Source/dsp/DryWetMixer.h` | 每個外掛後面的乾濕比混音（乾聲由處理圖自動延遲對齊） |
| `third_party/{rnnoise,onnxruntime,deepfilternet,fastenhancer,gtcrn}` | 降噪用的程式庫、DLL 與模型；來源與重建方式見各自 README |
| `tests/NoiseBench.cpp`、`tests/AecDriftTest.cpp` | 離線檢查：降噪效果／CPU、AEC 時脈追蹤（build `NoiseBench`、`AecDriftTest` 目標後執行） |
| `Source/ui/SettingsWindow.*` | 設定視窗：Audio / Routing / AEC / Plugins / Updates 分頁 |
| `Source/ui/AecMonitorWindow.*` | AEC 即時監控 |
| `third_party/webrtc-aec3` | AEC3（有本地修改過的 `CMakeLists.txt`） |
| `third_party/ASIOSDK` | 授權不能散佈，不進版控；有放才會開 ASIO |

處理圖：`勾選的輸入聲道組（可多組，相加）→ AecProcessor → NoiseReducer → 外掛 → 乾濕比混音 → 外掛 → …（略過用節點 bypass 旗標）→ OutputReferenceTap → 依路由送到各輸出聲道組`；各輸入的原音也可依路由直送輸出。處理圖的輸入／輸出節點只有「有勾選」的聲道，換算見 `AudioEngine::pairChannels`

## 改動時對應

- 新設定 → `AppSettings` 加 getter/setter（鍵名放 `Keys`）→ UI 呼叫 → 需要重建就 `getEngine().rebuildGraph()`
- 外掛鏈操作 → 走 `AudioEngine::addPlugin/removePlugin/...`，它會「先存狀態 → 改清單 → 重建」；不要直接改 `PluginChain` 後自己重建，外掛狀態會對錯位置
- 新降噪模型 → `ModelNoiseReducers` 加一個 `FramedNoiseReducer` 子類別＋`NoiseReducerFactory` 加一項（含比較表文字）；`Format.latencySamples` 要正確（混回原音靠它對齊），用 NoiseBench 的延遲實測核對；模型檔加進 `CMakeLists.txt` 的 `LIGHTHOST_MODEL_FILES` 與 `installer/LightHost.iss`；跑 `NoiseBench` 確認
- 發新版 → 改 `CMakeLists.txt` 與 `installer/LightHost.iss` 的版本號，Release 必須附上檔名以 `-Setup.exe` 結尾的安裝檔，舊版的自動更新才找得到
- 選單項目 → `IconMenu.cpp` 的 `LeftMenu` / `RightMenu` 與對應的 `handle...Menu`

## 即時音訊執行緒的規矩

`processBlock` 與它呼叫到的程式：

- 不配置記憶體（`std::vector` 在 `prepareToPlay` 預留好）
- 不用會等待的鎖；需要和 UI 執行緒共用的物件，UI 端先整組建好再交換（見 `AecProcessor::installEngine`），音訊端只 `try_lock`
- 跨執行緒讀寫的純量用 `std::atomic`
- 例外：ONNX Runtime、DeepFilterNet 推論時內部可能配置記憶體，無法避免；推論包在 try/catch 裡，出錯時該格讓原音通過

## 寫程式鐵則

- 先把假設講清楚；有多種解讀就列出來問
- 用最少的程式解決問題，不寫沒被要求的功能或彈性
- 只動該動的；看到無關的死碼提出來，不要順手刪
- 先定義怎樣算完成（編譯過、行為可驗證）再動手

## 建置

一定要在 `C:\Users\Taiyo\Desktop\lighthost` 執行。它是指向 `G:\程式集\lighthost` 的目錄連結；直接在中文路徑下跑，CMake 會在偵測 C 編譯器後當掉。

```
cmake -S . -B build-new -G "Visual Studio 17 2022" -A x64
cmake --build build-new --config Release --target LightHost --parallel
```

`CMakeLists.txt` 對 MSVC 開了 `/MP`（同一專案內平行編譯），加上 `--parallel`（多專案同時編），16 執行緒下全部重編約 30 秒。改版本號後要刪掉 `build-new/LightHost_artefacts/JuceLibraryCode/LightHost_resources.rc` 再編，執行檔的版本資訊才會更新。

產物：`build-new\LightHost_artefacts\Release\Light Host.exe`。舊的 `build/` 是在 `L:` 磁碟代號下建的，已經不能用。

## 當日 changelog

每次改完檔，追加到 `changelogs/YYYY-MM-DD.md`（台灣時間）。一筆一個 `## HH:MM — 一句話摘要`，下分「總體／檔案／更動」三段，用繁體中文；每個改動或新增的檔案都要列在「檔案」。
