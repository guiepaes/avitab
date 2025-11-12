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
#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "App.h"
#include "src/gui_toolkit/Timer.h"
#include "src/gui_toolkit/widgets/Window.h"
#include "src/gui_toolkit/widgets/TextArea.h"
#include "src/gui_toolkit/widgets/Keyboard.h"
#include "src/gui_toolkit/widgets/Button.h"
#include "src/gui_toolkit/widgets/Label.h"
#include "src/gui_toolkit/widgets/List.h"
#include "src/charts/RESTClient.h"

namespace avitab {

class YouTubeLiveApp : public App {
public:
    explicit YouTubeLiveApp(FuncsPtr appFuncs);
    ~YouTubeLiveApp() override;

    void suspend() override;

private:
    struct LiveData {
        bool success = false;
        std::string viewersText;
        std::string statusText;
        std::vector<std::string> comments;
    };

    void buildUI();
    void triggerRefresh();
    void startAutoRefresh();
    void stopAutoRefresh();
    bool onTimer();
    void runFetch(const std::string &apiKey, const std::string &videoId);
    LiveData downloadLiveData(const std::string &apiKey, const std::string &videoId);
    static std::string trim(const std::string &value);
    static std::string extractVideoId(const std::string &url);
    static std::string formatTimestamp();

    std::shared_ptr<Window> window;
    std::shared_ptr<TextArea> urlField;
    std::shared_ptr<TextArea> apiKeyField;
    std::shared_ptr<TextArea> intervalField;
    std::shared_ptr<Keyboard> keyboard;
    std::shared_ptr<Button> refreshButton;
    std::shared_ptr<Button> autoRefreshButton;
    std::shared_ptr<Label> statusLabel;
    std::shared_ptr<Label> viewersLabel;
    std::shared_ptr<Label> chatHeaderLabel;
    std::shared_ptr<List> commentsList;

    std::unique_ptr<Timer> refreshTimer;
    std::thread fetchThread;
    std::atomic<bool> requestInProgress{false};
    std::atomic<bool> autoRefreshEnabled{false};
    std::atomic<bool> shuttingDown{false};

    apis::RESTClient httpClient;
};

}  // namespace avitab