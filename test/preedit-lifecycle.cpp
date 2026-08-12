// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "test-input-context.h"

#include <iostream>
#include <string>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-preedit-lifecycle");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Smooth/Telex", "mode=Smooth, input method=Telex", "configured mode or input method differs",
                      "the lifecycle test cannot exercise Smooth Telex behavior");
        return 1;
    }
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    context->inputPanel().setClientPreedit(fcitx::Text("probe"));
    context->updatePreedit();
    if (context->preeditUpdates() != 1) {
        reportFailure("establish preedit update baseline", "updatePreedit count=1", "updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "the test recorder did not observe the initial client preedit update");
        return 1;
    }

    context->resetRecorder();
    fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, in);
    if (context->preeditUpdates() != 0) {
        reportFailure("activate input method", "updatePreedit count=0", "updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "activation unexpectedly refreshed client preedit");
        return 1;
    }

    context->focusOut();
    context->resetRecorder();
    fcitx::InputContextEvent out(context.get(), fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, out);
    if (context->preeditUpdates() != 0) {
        reportFailure("deactivate input method", "updatePreedit count=0", "updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "deactivation unexpectedly refreshed client preedit");
        return 1;
    }
    return 0;
}
