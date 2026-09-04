// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file preedit-lifecycle.cpp
 * @brief Headless integration test for client-side preedit lifecycle & focus transitions.
 *
 * Architectural Contract:
 * In Fcitx5, client applications supporting inline preedit (`CapabilityFlag::Preedit`)
 * render uncommitted composition directly inside their text widgets (e.g. Qt, GTK, Electron).
 *
 * When an input context undergoes focus or lifecycle transitions:
 * 1. Active Composition: Typing keys (e.g. 'a', 's' in Telex) must update the client
 *    preedit text to "á" without prematurely committing text to the document.
 * 2. Focus Loss (`FocusOut` / Reset): When the user switches windows or resets composition,
 *    an explicit reset must discard composition rather than committing it.
 *    On focus-out, the active preedit composition MUST be committed exactly once,
 *    and the client preedit buffer must be cleared.
 * 3. Focus Regain (`FocusIn` / Activate): When returning focus to the application, the
 *    preedit buffer must remain clean. No stale ghost text or duplicate commits should occur.
 *
 * This test uses `TestInputContext` and `TestInstance` to deterministically verify these
 * state transitions without a running X11/Wayland desktop session.
 */

#include "lotus-engine.h"
#include "test-input-context.h"

#include <fcitx-utils/utf8.h>
#include <iostream>
#include <string>
#include <vector>

namespace {

    // Helper to log explicit assertion failures with step, expected, actual, and business meaning.
    void reportFailure(const std::string& step, const std::string& expected, const std::string& actual, const std::string& meaning) {
        std::cerr << "Step: " << step << '\n';
        std::cerr << "Expected: " << expected << '\n';
        std::cerr << "Actual: " << actual << '\n';
        std::cerr << "Meaning: " << meaning << '\n';
    }

} // namespace

int main() {
    // Step 0: Sandbox filesystem paths to /tmp/fcitx5-lotus-preedit-lifecycle
    configureTestPaths("fcitx5-lotus-preedit-lifecycle");
    TestInstance       testInstance;
    fcitx::LotusEngine engine(&testInstance.instance);

    // Step 1: Configure Lotus in Preedit mode with Telex input method
    fcitx::RawConfig config;
    config.setValueByPath("Mode", "Preedit");
    config.setValueByPath("InputMethod", "Telex");
    engine.setConfig(config);
    if (fcitx::modeStringToEnum(engine.config().mode.value()) != fcitx::LotusMode::Preedit || engine.config().inputMethod.value() != "Telex") {
        reportFailure("configure Preedit/Telex", "mode=Preedit, input method=Telex", "configured mode or input method differs",
                      "the lifecycle test cannot exercise client preedit behavior");
        return 1;
    }

    // Step 2: Initialize mock InputContext with Preedit and ClientUnfocusCommit capabilities
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->setCapabilityFlags(fcitx::CapabilityFlags{fcitx::CapabilityFlag::Preedit, fcitx::CapabilityFlag::ClientUnfocusCommit});
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");

    fcitx::InputContextEvent in(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, in);
    context->resetPreeditUpdateCount();

    // Step 3: Type Telex keys 'a' followed by 's' -> should compose Vietnamese character "á"
    for (auto sym : {FcitxKey_a, FcitxKey_s}) {
        fcitx::KeyEvent event(context.get(), fcitx::Key(sym), false);
        engine.keyEvent(entry, event);
        if (!event.accepted()) {
            reportFailure("type Telex key", "key event accepted", "key " + std::to_string(sym) + " was not accepted", "Lotus did not process the Telex input");
            return 1;
        }
    }

    // Step 4: Verify client preedit state during active composition
    // - Client preedit must hold "á" (valid UTF-8, 2 bytes, cursor at end).
    // - Server preedit (candidate window) must be empty.
    // - Commits must be 0 (composition is still active, not yet committed).
    // - updatePreedit() must have been called twice (once for 'a', once for 's').
    const auto& clientPreedit = context->inputPanel().clientPreedit();
    const auto& serverPreedit = context->inputPanel().preedit();
    if (clientPreedit.toString() != "á" || !fcitx::utf8::validate(clientPreedit.toString()) || clientPreedit.cursor() != 2 || !context->commits().empty() ||
        !serverPreedit.toString().empty() || context->preeditUpdates() != 2) {
        reportFailure("type a then s in Preedit/Telex", "client preedit=á (valid UTF-8, cursor=2), server preedit empty, commits=0, updatePreedit count=2",
                      "client preedit=" + clientPreedit.toString() + ", cursor=" + std::to_string(clientPreedit.cursor()) + ", server preedit=" + serverPreedit.toString() +
                          ", commits=" + std::to_string(context->commits().size()) + ", updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "Preedit mode must expose the composed Telex result through client preedit without committing it");
        return 1;
    }

    // Step 5: Verify public reset behavior (InputContextReset)
    // - A public reset must discard rather than commit active composition.
    // - Client preedit must be cleared with one additional update call (total updates = 3).
    fcitx::InputContextEvent reset(context.get(), fcitx::EventType::InputContextReset);
    engine.reset(entry, reset);
    if (!context->commits().empty()) {
        reportFailure("InputContextReset", "no commits", "unexpected commit", "A public reset must discard rather than commit composition.");
        return 1;
    }
    if (!context->inputPanel().clientPreedit().toString().empty() || context->preeditUpdates() != 3) {
        reportFailure("InputContextReset", "client preedit empty, updatePreedit count=3",
                      "client preedit=" + context->inputPanel().clientPreedit().toString() + ", updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "a public reset must clear the client preedit with one additional update");
        return 1;
    }

    // Recompose 'a' + 's' to "á"
    for (auto sym : {FcitxKey_a, FcitxKey_s}) {
        fcitx::KeyEvent event(context.get(), fcitx::Key(sym), false);
        engine.keyEvent(entry, event);
        if (!event.accepted()) {
            reportFailure("recompose Telex key", "key event accepted", "key " + std::to_string(sym) + " was not accepted", "Lotus did not process the Telex input");
            return 1;
        }
    }

    // Step 6: Verify focus-out behavior (InputContextFocusOut)
    // - A focus-out reset must commit active client preedit exactly once and clear it.
    const std::vector<std::string> expectedCommits{"á"};
    context->focusOut();
    fcitx::InputContextEvent out(context.get(), fcitx::EventType::InputContextFocusOut);
    engine.reset(entry, out);
    if (context->commits() != expectedCommits || !context->inputPanel().clientPreedit().toString().empty() || context->preeditUpdates() != 6) {
        reportFailure("InputContextFocusOut", "commits=[á], client preedit empty, updatePreedit count=6",
                      "commit count=" + std::to_string(context->commits().size()) + (context->commits().empty() ? "" : ", first commit=" + context->commits().front()) +
                          ", client preedit=" + context->inputPanel().clientPreedit().toString() + ", updatePreedit count=" + std::to_string(context->preeditUpdates()),
                      "a focus-out reset must commit the active client preedit exactly once and clear it with one additional update");
        return 1;
    }

    // Step 7: Emulate FocusIn & reactivation (user refocuses the application)
    // - Engine reactivation must keep the preedit buffer empty without committing stale text.
    context->focusIn();
    fcitx::InputContextEvent reactivate(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, reactivate);
    if (!context->inputPanel().clientPreedit().toString().empty() || context->commits() != expectedCommits) {
        reportFailure("reactivate after focus out", "client preedit empty, commits=[á]",
                      "client preedit=" + context->inputPanel().clientPreedit().toString() + ", commit count=" + std::to_string(context->commits().size()),
                      "reactivation must preserve the cleared preedit without committing stale text");
        return 1;
    }

    return 0; // All lifecycle invariants verified successfully
}
