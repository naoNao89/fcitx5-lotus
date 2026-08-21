// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

class TestInputContext final : public fcitx::InputContext {
  public:
    explicit TestInputContext(fcitx::Instance* instance) : InputContext(instance->inputContextManager(), "test") {
        created();
    }
    ~TestInputContext() override {
        destroy();
    }

    const char* frontend() const override {
        return "test";
    }
    void commitStringImpl(const std::string& text) override {
        commits_.push_back(text);
    }
    void deleteSurroundingTextImpl(int offset, unsigned int size) override {
        deletes_.emplace_back(offset, size);
    }
    void forwardKeyImpl(const fcitx::ForwardKeyEvent& event) override {
        forwarded_.push_back(event);
    }
    void updatePreeditImpl() override {
        ++preeditUpdates_;
    }

    void resetPreeditUpdateCount() {
        preeditUpdates_ = 0;
    }
    unsigned int preeditUpdates() const {
        return preeditUpdates_;
    }
    const std::vector<std::string>& commits() const {
        return commits_;
    }
    const auto& deletes() const {
        return deletes_;
    }
    const auto& forwarded() const {
        return forwarded_;
    }

  private:
    std::vector<std::string>                  commits_;
    std::vector<std::pair<int, unsigned int>> deletes_;
    std::vector<fcitx::ForwardKeyEvent>       forwarded_;
    unsigned int                              preeditUpdates_ = 0;
};

inline void configureTestPaths(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "config/fcitx5/conf");
    std::filesystem::create_directories(root / "data");
    std::filesystem::create_directories(root / "cache");
    setenv("HOME", root.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1);
}

struct TestInstance {
    TestInstance() : instance(2, argv) {
        instance.addonManager().registerDefaultLoader(nullptr);
        instance.initialize();
    }
    char            program[64]    = "lotus-headless-test";
    char            disableAll[16] = "--disable=all";
    char*           argv[3]        = {program, disableAll, nullptr};
    fcitx::Instance instance;
};
