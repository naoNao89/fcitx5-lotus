// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emoji mode + double-tap shortcuts: the double-space/double-hyphen
// shortcuts run before handleEmojiMode and must exclude Emoji mode. Emoji
// mode buffers keys in its own preedit without delivering them to the app,
// so firing a replacement on the second key would backspace/delete text the
// user typed before the emoji buffer.
#include "fcitx-utils/keysym.h"
#include "kb-socket-listener.h"
#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx/inputpanel.h>

#include <memory>
#include <string>

namespace {

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        return event.accepted();
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-emoji-double-hyphen-mode-clash");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);
    fcitx::RawConfig   config;
    config.setValueByPath("Mode", "Emoji Picker");
    config.setValueByPath("DoubleHyphenToEmDash", "True");
    config.setValueByPath("DoubleSpaceToPeriod", "True");
    engine.setConfig(config);
    if (engine.config().mode.value() != fcitx::LotusMode::Emoji) {
        reportFailure("configure Emoji Picker", "mode=Emoji Picker", "configured mode differs", "the test cannot exercise Emoji mode");
        return 1;
    }

    KbSocketListener listener;
    if (!listener.valid())
        return 1;

    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);

    // First '-' must stay inside the emoji preedit buffer.
    if (!send(engine, entry, *context, FcitxKey_minus)) {
        reportFailure("type first '-' in Emoji mode", "key accepted (buffered in emoji preedit)", "key rejected", "the first hyphen did not enter the emoji buffer");
        return 1;
    }
    // Emoji mode renders its buffer via the server-side panel preedit
    // (setPreedit in updateEmojiPreedit), not the client preedit.
    const std::string preeditAfterFirst = context->inputPanel().preedit().toString();
    if (preeditAfterFirst != "-") {
        reportFailure("first '-' buffered in emoji preedit", "preedit='-'", "preedit='" + preeditAfterFirst + "'",
                      "the emoji preedit must hold the hyphen instead of forwarding it");
        return 1;
    }

    // Second '-': the double-hyphen shortcut must not hijack it here. In Emoji
    // mode the replacement backspaces would hit text the user typed before
    // the emoji preedit, so no replacement request may reach the uinput
    // socket and the key must reach the emoji buffer.
    const bool        acceptedSecond = send(engine, entry, *context, FcitxKey_minus);
    KbMsg             msg{};
    const bool        replacementFired   = listener.tryReceive(msg);
    const std::string preeditAfterSecond = context->inputPanel().preedit().toString();
    const bool        committed          = !context->commits().empty() || !context->deletes().empty();

    if (replacementFired || committed || !acceptedSecond || preeditAfterSecond != "--") {
        reportFailure("type second '-' in Emoji mode", "no uinput request, no commit/delete, key accepted, preedit='--'",
                      "uinput request fired=" + std::string(replacementFired ? "yes" : "no") + ", commits/deletes=" + std::string(committed ? "yes" : "no") +
                          ", accepted=" + std::string(acceptedSecond ? "yes" : "no") + ", preedit='" + preeditAfterSecond + "'",
                      "the double-hyphen shortcut stole the key from Emoji mode and issued a replacement against text "
                      "that does not belong to the emoji buffer");
        return 1;
    }

    // Same class check for double-space, fully independent of the emoji DB:
    // Escape clears the emoji buffer and candidates, so with an empty buffer
    // and no history there is nothing to select and space must forward.
    // The double-space shortcut must not intercept the second space and fire
    // a period replacement against text outside the emoji buffer.
    if (!send(engine, entry, *context, FcitxKey_Escape)) {
        reportFailure("escape clears emoji buffer", "key accepted", "key rejected", "Escape must clear the emoji buffer between the scenarios");
        return 1;
    }
    const bool forwardedFirstSpace   = !send(engine, entry, *context, FcitxKey_space);
    const bool forwardedSecondSpace  = !send(engine, entry, *context, FcitxKey_space);
    const bool spaceReplacementFired = listener.tryReceive(msg);
    const bool spaceCommitted        = !context->commits().empty() || !context->deletes().empty();

    if (!forwardedFirstSpace || !forwardedSecondSpace || spaceReplacementFired || spaceCommitted) {
        reportFailure("type two spaces in Emoji mode", "both spaces forwarded, no uinput request, no commit/delete",
                      "first space forwarded=" + std::string(forwardedFirstSpace ? "yes" : "no") + ", second space forwarded=" + std::string(forwardedSecondSpace ? "yes" : "no") +
                          ", uinput request fired=" + std::string(spaceReplacementFired ? "yes" : "no") + ", commits/deletes=" + std::string(spaceCommitted ? "yes" : "no"),
                      "the double-space shortcut stole the second space from Emoji mode and issued a replacement "
                      "against text that does not belong to the emoji buffer");
        return 1;
    }
}
