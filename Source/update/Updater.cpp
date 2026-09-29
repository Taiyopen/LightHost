#include "Updater.h"

namespace
{
    constexpr auto latestReleaseApi = "https://api.github.com/repos/Taiyopen/LightHost/releases/latest";
    // GitHub API 沒有 User-Agent 會拒絕
    constexpr auto httpHeaders = "User-Agent: LightHost-Updater\r\nAccept: application/vnd.github+json";

    constexpr int firstCheckDelayMs = 15 * 1000;
    constexpr int checkIntervalMs = 6 * 60 * 60 * 1000;

    /** 在背景執行緒跑；失敗時 version 為空、error 說明原因 */
    ReleaseInfo fetchLatestRelease (juce::String& error)
    {
        int statusCode = 0;
        auto stream = juce::URL (latestReleaseApi)
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                  .withExtraHeaders (httpHeaders)
                                                  .withConnectionTimeoutMs (10000)
                                                  .withStatusCode (&statusCode));

        if (stream == nullptr)
        {
            error = "Could not connect to GitHub.";
            return {};
        }

        const auto body = stream->readEntireStreamAsString();

        if (statusCode != 200)
        {
            error = "GitHub returned HTTP " + juce::String (statusCode) + ".";
            return {};
        }

        const auto json = juce::JSON::parse (body);
        ReleaseInfo info;
        info.version = json.getProperty ("tag_name", {}).toString().trimCharactersAtStart ("vV");

        if (auto* assets = json.getProperty ("assets", {}).getArray())
        {
            for (const auto& asset : *assets)
            {
                if (asset.getProperty ("name", {}).toString().endsWithIgnoreCase ("-Setup.exe"))
                {
                    info.installerUrl = juce::URL (asset.getProperty ("browser_download_url", {}).toString());
                    info.installerSize = static_cast<juce::int64> (asset.getProperty ("size", 0));
                    break;
                }
            }
        }

        if (info.version.isEmpty() || info.installerUrl.isEmpty())
        {
            error = "The latest release has no installer.";
            return {};
        }

        return info;
    }
}

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto partsA = juce::StringArray::fromTokens (a.trimCharactersAtStart ("vV"), ".", {});
    const auto partsB = juce::StringArray::fromTokens (b.trimCharactersAtStart ("vV"), ".", {});

    // 超出範圍的段落 StringArray 會回傳空字串，當作 0
    for (int i = 0; i < juce::jmax (partsA.size(), partsB.size()); ++i)
    {
        const int x = partsA[i].getIntValue();
        const int y = partsB[i].getIntValue();

        if (x != y)
            return x < y ? -1 : 1;
    }

    return 0;
}

Updater::Updater()
{
    weakThis = this;
}

Updater::~Updater()
{
    stopTimer();
    download.reset();              // 會等下載執行緒結束，之後不會再有 finished() 回呼
    pool.removeAllJobs (true, 15000);
}

juce::String Updater::getCurrentVersion()
{
    return ProjectInfo::versionString;
}

void Updater::setAutoCheck (bool enabled)
{
    if (enabled)
        startTimer (firstAutoCheckDone ? checkIntervalMs : firstCheckDelayMs);
    else
        stopTimer();
}

void Updater::timerCallback()
{
    if (! firstAutoCheckDone)
    {
        firstAutoCheckDone = true;
        startTimer (checkIntervalMs);
    }

    checkNow();
}

void Updater::setState (State newState)
{
    state = newState;
    sendChangeMessage();
}

void Updater::checkNow()
{
    if (state == State::checking || state == State::downloading || state == State::installerLaunched)
        return;

    setState (State::checking);

    pool.addJob ([weak = weakThis]
    {
        juce::String error;
        const auto latest = fetchLatestRelease (error);

        juce::MessageManager::callAsync ([weak, latest, error]
        {
            if (auto* updater = weak.get())
                updater->handleCheckResult (latest, error);
        });
    });
}

void Updater::handleCheckResult (const ReleaseInfo& latest, const juce::String& error)
{
    if (latest.version.isEmpty())
    {
        errorMessage = error;
        setState (State::checkFailed);
    }
    else if (compareVersions (latest.version, getCurrentVersion()) > 0)
    {
        release = latest;
        setState (State::available);
    }
    else
    {
        setState (State::upToDate);
    }
}

void Updater::downloadAndInstall()
{
    if (! canInstall())
        return;

    installerFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("LightHost-" + release.version + "-Setup.exe");
    installerFile.deleteFile();

    setState (State::downloading);

    download = release.installerUrl.downloadToFile (installerFile,
                                                    juce::URL::DownloadTaskOptions()
                                                        .withExtraHeaders (httpHeaders)
                                                        .withListener (this));

    if (download == nullptr)
    {
        errorMessage = "Could not start the download.";
        setState (State::downloadFailed);
    }
}

void Updater::finished (juce::URL::DownloadTask*, bool success)
{
    // 這裡在下載執行緒上，轉回訊息執行緒再處理
    juce::MessageManager::callAsync ([weak = weakThis, success]
    {
        if (auto* updater = weak.get())
            updater->handleDownloadFinished (success);
    });
}

void Updater::handleDownloadFinished (bool success)
{
    download.reset();

    const bool sizeMatches = release.installerSize <= 0 || installerFile.getSize() == release.installerSize;

    if (! success || ! installerFile.existsAsFile() || ! sizeMatches)
    {
        errorMessage = "The download did not finish.";
        setState (State::downloadFailed);
        return;
    }

    // 安裝檔需要系統管理員權限：startAsProcess 走 ShellExecute，會跳 UAC；使用者按取消時回傳 false
    if (installerFile.startAsProcess ("/SILENT /SUPPRESSMSGBOXES /NORESTART"))
    {
        setState (State::installerLaunched);
    }
    else
    {
        errorMessage = "The installer did not start (the permission prompt may have been cancelled).";
        setState (State::downloadFailed);
    }
}
