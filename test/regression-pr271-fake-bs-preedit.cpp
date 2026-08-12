// SPDX-License-Identifier: GPL-3.0-or-later
// Headless regression coverage for PR #271.

#include "lotus-engine.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/instance.h>
#include <fcitx/addonmanager.h>

namespace {

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
            committed_ += text;
        }
        void deleteSurroundingTextImpl(int, unsigned int) override {}
        void forwardKeyImpl(const fcitx::ForwardKeyEvent&) override {}
        void updatePreeditImpl() override {
            ++preeditUpdates_;
        }

        void resetRecorder() {
            preeditUpdates_ = 0;
        }
        unsigned int preeditUpdates() const {
            return preeditUpdates_;
        }

      private:
        std::string  committed_;
        unsigned int preeditUpdates_ = 0;
    };

    void setIsolatedStandardPaths() {
        const auto root = std::filesystem::temp_directory_path() / "fcitx5-lotus-pr271";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "config/fcitx5/conf");
        std::filesystem::create_directories(root / "data");
        std::filesystem::create_directories(root / "cache");
        std::ofstream(root / "config/fcitx5/conf/lotus.conf") << "[General]\n"
                                                              << "Mode=Smooth\n"
                                                              << "InputMethod=Telex\n";
        setenv("HOME", root.c_str(), 1);
        setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
        setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
        setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1);
    }

    bool expectZero(const char* operation, unsigned int before, unsigned int after) {
        if (after == before) {
            return true;
        }
        std::cerr << operation << " unexpectedly called updatePreedit " << (after - before) << " time(s)\n";
        return false;
    }

} // namespace

int main() {
    // StandardPaths reads the environment during Instance construction.
    setIsolatedStandardPaths();

    char            program[]    = "regression_pr271_fake_bs_preedit";
    char            disableAll[] = "--disable=all";
    char*           argv[]       = {program, disableAll, nullptr};
    fcitx::Instance instance(2, argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    fcitx::LotusEngine engine(&instance);
    auto               context = std::make_unique<TestInputContext>(&instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    // Proves the recorder observes the real InputContext callback.
    context->updatePreedit();
    if (context->preeditUpdates() != 1) {
        std::cerr << "direct updatePreedit did not reach TestInputContext\n";
        return 1;
    }

    context->resetRecorder();
    fcitx::InputContextEvent focusIn(context.get(), fcitx::EventType::InputContextFocusIn);
    const auto               activateBefore = context->preeditUpdates();
    engine.activate(entry, focusIn);
    if (!expectZero("activate", activateBefore, context->preeditUpdates())) {
        return 1;
    }

    context->resetRecorder();
    fcitx::InputContextEvent focusOut(context.get(), fcitx::EventType::InputContextFocusOut);
    const auto               deactivateBefore = context->preeditUpdates();
    engine.deactivate(entry, focusOut);
    if (!expectZero("deactivate", deactivateBefore, context->preeditUpdates())) {
        return 1;
    }

    // Declarations make the context destroy before its engine and instance.
    return 0;
}
