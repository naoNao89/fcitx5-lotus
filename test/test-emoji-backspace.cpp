// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/utf8.h>
#include <iostream>
#include <memory>
#include <string>

namespace {

    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

    bool sendKey(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (!event.accepted()) {
            reportFailure("send key " + std::to_string(symbol), "key accepted", "key rejected", "Emoji-mode key handler did not accept key");
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-emoji-backspace");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Emoji Picker");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Emoji) {
        reportFailure("configure Emoji Picker", "mode=Emoji", "configured mode differs", "cannot enter emoji mode");
        return 1;
    }

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlag::Preedit);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // Type 'a' (1-byte), then 'é' (2-byte UTF-8: \xc3\xa9)
    if (!sendKey(engine, entry, *context, FcitxKey_a) || !sendKey(engine, entry, *context, FcitxKey_eacute)) {
        return 1;
    }

    // Press Backspace once to erase 'é'
    if (!sendKey(engine, entry, *context, FcitxKey_BackSpace)) {
        return 1;
    }

    // Press Return to commit the remaining buffer
    if (!sendKey(engine, entry, *context, FcitxKey_Return)) {
        return 1;
    }

    if (context->commits().empty()) {
        reportFailure("commit emoji buffer", "commits non-empty", "commits empty", "Return did not commit buffer");
        return 1;
    }

    const std::string committed = context->commits().back();
    if (!fcitx::utf8::validate(committed) || committed != "a") {
        reportFailure("backspace multi-byte UTF-8 in emoji mode", "valid UTF-8 'a'", "committed='" + committed + "' (length=" + std::to_string(committed.length()) + ")",
                      "backspace must erase the complete multi-byte UTF-8 codepoint without leaving dangling lead bytes");
        return 1;
    }

    return 0;
}
