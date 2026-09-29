#pragma once

#include <JuceHeader.h>

/** 比較 "1.4.0"、"v1.10.2" 這類版本號；a 較新回傳正數，相同回傳 0，較舊回傳負數 */
int compareVersions (const juce::String& a, const juce::String& b);

struct ReleaseInfo
{
    juce::String version;          // 不含開頭的 v
    juce::URL installerUrl;
    juce::int64 installerSize = 0; // 0 代表 GitHub 沒給大小
};

/**
 * 到 GitHub 查最新版本，有新版時下載安裝檔並執行。
 * 網路工作都在背景執行緒；狀態改變時在訊息執行緒送出 change message。
 */
class Updater : public juce::ChangeBroadcaster,
                private juce::Timer,
                private juce::URL::DownloadTaskListener
{
public:
    enum class State
    {
        idle,
        checking,
        upToDate,
        available,
        downloading,
        downloadFailed,     // 下載或啟動安裝檔失敗；仍可再按安裝
        installerLaunched,  // 安裝檔已啟動，App 應該存檔並結束
        checkFailed
    };

    Updater();
    ~Updater() override;

    /** 開啟時：第一次在 15 秒後檢查，之後每 6 小時 */
    void setAutoCheck (bool enabled);
    void checkNow();
    /** 只有在 available / downloadFailed 時有作用 */
    void downloadAndInstall();

    State getState() const { return state; }
    bool canInstall() const { return state == State::available || state == State::downloadFailed; }
    const ReleaseInfo& getAvailableRelease() const { return release; }
    juce::String getErrorMessage() const { return errorMessage; }

    static juce::String getCurrentVersion();

private:
    void timerCallback() override;
    void finished (juce::URL::DownloadTask*, bool success) override;
    void setState (State newState);
    void handleCheckResult (const ReleaseInfo& latest, const juce::String& error);
    void handleDownloadFinished (bool success);

    State state = State::idle;
    ReleaseInfo release;
    juce::String errorMessage;
    juce::File installerFile;
    std::unique_ptr<juce::URL::DownloadTask> download;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    bool firstAutoCheckDone = false;

    // 在建構時就建立，背景執行緒只複製它，不在背景執行緒第一次建立弱參照
    juce::WeakReference<Updater> weakThis;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Updater)
    JUCE_DECLARE_NON_COPYABLE (Updater)
};
