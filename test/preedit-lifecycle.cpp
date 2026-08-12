// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "test-input-context.h"

#include <iostream>

int main() {
    configureTestPaths("fcitx5-lotus-preedit-lifecycle");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Uinput (Smooth)");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Smooth || engine.config().inputMethod.value() != "Telex") {
        std::cerr << "failed to configure Smooth/Telex\n";
        return 1;
    }
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry entry("lotus", "Lotus", "vi", "lotus");

    context->inputPanel().setClientPreedit(fcitx::Text("probe"));
    context->updatePreedit();
    if (context->preeditUpdates() != 1)
        return 1;

    context->resetRecorder();
    fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, in);
    if (context->preeditUpdates() != 0) {
        std::cerr << "activate updated preedit\n";
        return 1;
    }

    context->focusOut();
    context->resetRecorder();
    fcitx::InputContextEvent out(context.get(), fcitx::EventType::InputContextFocusOut);
    engine.deactivate(entry, out);
    if (context->preeditUpdates() != 0) {
        std::cerr << "deactivate updated preedit\n";
        return 1;
    }
    return 0;
}
