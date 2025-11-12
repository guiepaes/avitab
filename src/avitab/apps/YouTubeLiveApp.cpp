/*
 *   AviTab - Aviator's Virtual Tablet
 *   Copyright (C) 2018-2024 Folke Will <folko@solhost.org>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Affero General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Affero General Public License for more details.
 *
 *   You should have received a copy of the GNU Affero General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "YouTubeLiveApp.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace avitab {
namespace {
constexpr int kDefaultIntervalMinutes = 1;
constexpr int kMaxComments = 25;
}

YouTubeLiveApp::YouTubeLiveApp(FuncsPtr appFuncs):
    App(appFuncs)
{
    httpClient.setVerbose(false);
    buildUI();
}

YouTubeLiveApp::~YouTubeLiveApp() {
    shuttingDown = true;
    stopAutoRefresh();
    if (fetchThread.joinable()) {
        fetchThread.join();
    }
}

void YouTubeLiveApp::suspend() {
    stopAutoRefresh();
    App::suspend();
}

void YouTubeLiveApp::buildUI() {
    auto container = getUIContainer();
    window = std::make_shared<Window>(container, "YouTube Live");
    window->setOnClose([this] () {
        stopAutoRefresh();
        exit();
    });

    auto instructions = std::make_shared<Label>(window, "Enter your live URL and YouTube Data API v3 key.");
    instructions->setLongMode(true);
    instructions->setDimensions(window->getContentWidth(), 40);
    instructions->alignInTopLeft();

    auto urlLabel = std::make_shared<Label>(window, "Live URL:");
    urlLabel->alignBelow(instructions, 6);

    urlField = std::make_shared<TextArea>(window, "");
    urlField->setMultiLine(false);
    urlField->setShowCursor(true);
    urlField->setDimensions(window->getContentWidth(), 35);
    urlField->alignBelow(urlLabel, 2);

    auto apiLabel = std::make_shared<Label>(window, "API key:");
    apiLabel->alignBelow(urlField, 6);

    apiKeyField = std::make_shared<TextArea>(window, "");
    apiKeyField->setMultiLine(false);
    apiKeyField->setShowCursor(true);
    apiKeyField->setDimensions(window->getContentWidth(), 35);
    apiKeyField->alignBelow(apiLabel, 2);

    auto intervalLabel = std::make_shared<Label>(window, "Refresh interval (min):");
    intervalLabel->alignBelow(apiKeyField, 6);

    intervalField = std::make_shared<TextArea>(window, std::to_string(kDefaultIntervalMinutes));
    intervalField->setMultiLine(false);
    intervalField->setShowCursor(true);
    intervalField->setDimensions(120, 35);
    intervalField->alignBelow(intervalLabel, 2);

    refreshButton = std::make_shared<Button>(window, "Refresh now");
    refreshButton->alignRightOf(intervalField, 12);
    refreshButton->setCallback([this] (const Button &) {
        triggerRefresh();
    });

    autoRefreshButton = std::make_shared<Button>(window, "Start auto refresh");
    autoRefreshButton->alignRightOf(refreshButton, 12);
    autoRefreshButton->setCallback([this] (const Button &) {
        if (autoRefreshEnabled.load()) {
            stopAutoRefresh();
        } else {
            startAutoRefresh();
        }
    });

    statusLabel = std::make_shared<Label>(window, "Ready");
    statusLabel->setLongMode(true);
    statusLabel->setDimensions(window->getContentWidth(), 35);
    statusLabel->alignBelow(intervalField, 45);

    viewersLabel = std::make_shared<Label>(window, "Concurrent viewers: --");
    viewersLabel->setDimensions(window->getContentWidth(), 30);
    viewersLabel->alignBelow(statusLabel, 6);

    chatHeaderLabel = std::make_shared<Label>(window, "Live chat:");
    chatHeaderLabel->alignBelow(viewersLabel, 4);

    commentsList = std::make_shared<List>(window);
    commentsList->alignBelow(chatHeaderLabel, 2);

    keyboard = std::make_shared<Keyboard>(window, urlField);
    keyboard->setPosition(0, window->getContentHeight() - keyboard->getHeight());
    keyboard->setOnCancel([] () {});

    auto keyboardTop = keyboard->getY();
    int availableHeight = keyboardTop - commentsList->getY() - 6;
    if (availableHeight < 40) {
        availableHeight = 40;
    }
    commentsList->setDimensions(window->getContentWidth(), availableHeight);

    auto setTarget = [this] (std::shared_ptr<TextArea> target) {
        if (keyboard) {
            keyboard->setTarget(target);
        }
    };

    urlField->setClickable(true);
    urlField->setClickHandler([setTarget, field=urlField] (int, int, bool press, bool) {
        if (press) {
            setTarget(field);
        }
    });

    apiKeyField->setClickable(true);
    apiKeyField->setClickHandler([setTarget, field=apiKeyField] (int, int, bool press, bool) {
        if (press) {
            setTarget(field);
        }
    });

    intervalField->setClickable(true);
    intervalField->setClickHandler([setTarget, field=intervalField] (int, int, bool press, bool) {
        if (press) {
            setTarget(field);
        }
    });

    commentsList->add("No live chat messages available.", -1);
}

void YouTubeLiveApp::triggerRefresh() {
    if (requestInProgress.exchange(true)) {
        if (statusLabel) {
            statusLabel->setText("Update already in progress...");
        }
        return;
    }

    std::string apiKey = trim(apiKeyField ? apiKeyField->getText() : "");
    std::string liveUrl = trim(urlField ? urlField->getText() : "");

    if (apiKey.empty() || liveUrl.empty()) {
        if (statusLabel) {
            statusLabel->setText("Please provide both the live URL and API key.");
        }
        requestInProgress = false;
        return;
    }

    auto videoId = extractVideoId(liveUrl);
    if (videoId.empty()) {
        if (statusLabel) {
            statusLabel->setText("Unable to determine the video ID.");
        }
        requestInProgress = false;
        return;
    }

    if (statusLabel) {
        statusLabel->setText("Updating...");
    }
    if (viewersLabel) {
        viewersLabel->setText("Concurrent viewers: --");
    }

    if (commentsList) {
        commentsList->clear();
        commentsList->add("Loading live chat messages...", -1);
    }

    if (fetchThread.joinable()) {
        fetchThread.join();
    }

    fetchThread = std::thread([this, apiKey, videoId]() {
        runFetch(apiKey, videoId);
    });
}

void YouTubeLiveApp::startAutoRefresh() {
    std::string intervalText = trim(intervalField ? intervalField->getText() : "");
    if (intervalText.empty()) {
        intervalText = std::to_string(kDefaultIntervalMinutes);
    }

    double minutes = 0.0;
    try {
        minutes = std::stod(intervalText);
    } catch (const std::exception &) {
        if (statusLabel) {
            statusLabel->setText("Invalid refresh interval. Enter minutes.");
        }
        return;
    }

    if (minutes <= 0.0) {
        if (statusLabel) {
            statusLabel->setText("The interval must be greater than zero.");
        }
        return;
    }

    int intervalMs = static_cast<int>(minutes * 60 * 1000);
    autoRefreshEnabled = true;
    refreshTimer = std::make_unique<Timer>(std::bind(&YouTubeLiveApp::onTimer, this), intervalMs);
    if (autoRefreshButton) {
        autoRefreshButton->setText("Stop auto refresh");
    }
    triggerRefresh();
}

void YouTubeLiveApp::stopAutoRefresh() {
    autoRefreshEnabled = false;
    if (refreshTimer) {
        refreshTimer->stop();
        refreshTimer.reset();
    }
    if (autoRefreshButton) {
        autoRefreshButton->setText("Start auto refresh");
    }
}

bool YouTubeLiveApp::onTimer() {
    if (!autoRefreshEnabled.load() || shuttingDown.load()) {
        return false;
    }
    triggerRefresh();
    return autoRefreshEnabled.load();
}

void YouTubeLiveApp::runFetch(const std::string &apiKey, const std::string &videoId) {
    LiveData data;

    if (!shuttingDown.load()) {
        try {
            data = downloadLiveData(apiKey, videoId);
        } catch (const std::exception &e) {
            data.statusText = std::string("Error: ") + e.what();
        }
    }

    auto statusWeak = std::weak_ptr<Label>(statusLabel);
    auto viewersWeak = std::weak_ptr<Label>(viewersLabel);
    auto listWeak = std::weak_ptr<List>(commentsList);

    requestInProgress = false;

    if (shuttingDown.load()) {
        return;
    }

    api().executeLater([statusWeak, viewersWeak, listWeak, data]() mutable {
        if (auto status = statusWeak.lock()) {
            status->setText(data.statusText.empty() ? "Ready" : data.statusText);
        }
        if (auto viewers = viewersWeak.lock()) {
            viewers->setText(data.viewersText.empty() ? "Concurrent viewers: --" : data.viewersText);
        }
        if (auto list = listWeak.lock()) {
            list->clear();
            if (data.comments.empty()) {
                list->add("No live chat messages available.", -1);
            } else {
                int idx = 0;
                for (const auto &entry : data.comments) {
                    list->add(entry, idx++);
                    if (idx >= kMaxComments) {
                        break;
                    }
                }
            }
        }
    });
}

YouTubeLiveApp::LiveData YouTubeLiveApp::downloadLiveData(const std::string &apiKey, const std::string &videoId) {
    LiveData result;
    bool cancel = false;

    std::string videosUrl = "https://www.googleapis.com/youtube/v3/videos?part=liveStreamingDetails&id=" + videoId + "&key=" + apiKey;
    std::string videoResponse = httpClient.get(videosUrl, cancel);

    auto videoJson = nlohmann::json::parse(videoResponse);
    if (!videoJson.contains("items") || videoJson["items"].empty()) {
        result.statusText = "Live stream not found.";
        return result;
    }

    const auto &item = videoJson["items"].front();
    if (item.contains("liveStreamingDetails")) {
        const auto &details = item["liveStreamingDetails"];
        if (details.contains("concurrentViewers")) {
            result.viewersText = "Concurrent viewers: " + details["concurrentViewers"].get<std::string>();
        } else {
            result.viewersText = "Concurrent viewers: n/a";
        }

        if (details.contains("activeLiveChatId")) {
            std::string chatId = details["activeLiveChatId"].get<std::string>();
            std::string chatUrl = "https://www.googleapis.com/youtube/v3/liveChat/messages?part=snippet,authorDetails&maxResults=" + std::to_string(kMaxComments) + "&liveChatId=" + chatId + "&key=" + apiKey;
            std::string chatResponse = httpClient.get(chatUrl, cancel);
            auto chatJson = nlohmann::json::parse(chatResponse);
            if (chatJson.contains("items")) {
                for (const auto &msg : chatJson["items"]) {
                    std::string author = "Unknown";
                    std::string message = "";
                    if (msg.contains("authorDetails") && msg["authorDetails"].contains("displayName")) {
                        author = msg["authorDetails"]["displayName"].get<std::string>();
                    }
                    if (msg.contains("snippet") && msg["snippet"].contains("displayMessage")) {
                        message = msg["snippet"]["displayMessage"].get<std::string>();
                    }
                    if (!message.empty()) {
                        result.comments.push_back(author + ": " + message);
                    }
                    if (result.comments.size() >= static_cast<size_t>(kMaxComments)) {
                        break;
                    }
                }
            }
        } else {
            result.comments.push_back("Live chat is not active for this stream.");
        }
    } else {
        result.viewersText = "Concurrent viewers: unavailable";
        result.comments.push_back("Live stream does not expose live chat data.");
    }

    if (result.comments.empty()) {
        result.comments.push_back("No live chat messages available.");
    }

    result.statusText = std::string("Last update: ") + formatTimestamp();
    result.success = true;
    return result;
}

std::string YouTubeLiveApp::trim(const std::string &value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [] (unsigned char ch) { return std::isspace(ch); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [] (unsigned char ch) { return std::isspace(ch); }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string YouTubeLiveApp::extractVideoId(const std::string &url) {
    std::string trimmed = trim(url);
    if (trimmed.empty()) {
        return "";
    }

    auto isCandidate = [] (const std::string &candidate) {
        if (candidate.size() != 11) {
            return false;
        }
        return std::all_of(candidate.begin(), candidate.end(), [] (unsigned char ch) {
            return std::isalnum(ch) || ch == '-' || ch == '_';
        });
    };

    if (isCandidate(trimmed)) {
        return trimmed;
    }

    auto findId = [&trimmed, &isCandidate] (const std::string &key) {
        auto pos = trimmed.find(key);
        if (pos == std::string::npos) {
            return std::string();
        }
        pos += key.length();
        auto end = trimmed.find_first_of("?&#", pos);
        std::string candidate = trimmed.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (isCandidate(candidate)) {
            return candidate;
        }
        return std::string();
    };

    std::string id = findId("v=");
    if (!id.empty()) {
        return id;
    }

    id = findId("youtu.be/");
    if (!id.empty()) {
        return id;
    }

    id = findId("/embed/");
    if (!id.empty()) {
        return id;
    }

    return "";
}

std::string YouTubeLiveApp::formatTimestamp() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    std::time_t t = clock::to_time_t(now);
    std::tm tmStruct{};
#if defined(_WIN32)
    localtime_s(&tmStruct, &t);
#else
    localtime_r(&t, &tmStruct);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tmStruct, "%H:%M:%S");
    return ss.str();
}

}  // namespace avitab