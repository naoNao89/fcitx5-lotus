// SPDX-License-Identifier: GPL-3.0-or-later
#include "lotus-engine.h"
#include "lotus-utils.h"
#include "test-input-context.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

    class BackspaceListener {
      public:
        BackspaceListener() {
            fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            if (fd_ < 0) {
                fail("socket");
                return;
            }
            sockaddr_un address{};
            address.sun_family    = AF_UNIX;
            const auto socketPath = buildSocketPath("kb_socket");
            address.sun_path[0]   = '\0';
            std::memcpy(&address.sun_path[1], socketPath.data(), socketPath.size());
            const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + socketPath.size() + 1);
            if (bind(fd_, reinterpret_cast<const sockaddr*>(&address), length) < 0 || listen(fd_, 1) < 0) {
                fail("bind/listen");
            }
        }

        ~BackspaceListener() {
            if (client_ >= 0)
                close(client_);
            if (fd_ >= 0)
                close(fd_);
        }

        bool receive(int& count) {
            if (client_ < 0) {
                pollfd pollfd{fd_, POLLIN, 0};
                if (fd_ < 0 || poll(&pollfd, 1, 2000) != 1 || !(pollfd.revents & POLLIN)) {
                    std::cerr << "socket accept timeout/error: " << std::strerror(errno) << '\n';
                    return false;
                }
                client_ = accept(fd_, nullptr, nullptr);
                if (client_ < 0) {
                    fail("accept");
                    return false;
                }
            }
            pollfd pollfd{client_, POLLIN, 0};
            if (poll(&pollfd, 1, 2000) != 1 || !(pollfd.revents & POLLIN) || recv(client_, &count, sizeof(count), 0) != sizeof(count)) {
                std::cerr << "socket receive failed: " << std::strerror(errno) << '\n';
                return false;
            }
            return true;
        }

        bool valid() const {
            return fd_ >= 0;
        }

      private:
        void fail(const char* operation) {
            std::cerr << operation << " failed: " << std::strerror(errno) << '\n';
            close(fd_);
            fd_ = -1;
        }

        int fd_     = -1;
        int client_ = -1;
    };

    bool send(fcitx::LotusEngine& engine, const fcitx::InputMethodEntry& entry, TestInputContext& context, fcitx::KeySym symbol, bool requireAccepted) {
        fcitx::KeyEvent event(&context, fcitx::Key(symbol), false);
        engine.keyEvent(entry, event);
        if (event.accepted() != requireAccepted) {
            std::cerr << "key=" << symbol << " accepted=" << event.accepted() << " expected=" << requireAccepted << " commits=" << context.commits().size() << '\n';
            return false;
        }
        return true;
    }

} // namespace

int main() {
    configureTestPaths("fcitx5-lotus-smooth-buffered-key-replay");
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

    BackspaceListener listener;
    if (!listener.valid())
        return 1;
    auto context = std::make_unique<TestInputContext>(&testInstance.instance);
    context->focusIn();
    fcitx::InputMethodEntry  entry("lotus", "Lotus", "vi", "lotus");
    fcitx::InputContextEvent focus(context.get(), fcitx::EventType::InputContextFocusIn);
    engine.activate(entry, focus);
    context->resetRecorder();

    // Telex a, s changes the real Bamboo preedit a -> á. Smooth mode replaces
    // the old character through the kb_socket transport.
    if (!send(engine, entry, *context, FcitxKey_a, false) || !send(engine, entry, *context, FcitxKey_s, true))
        return 1;
    int backspaces = 0;
    if (!listener.receive(backspaces) || backspaces <= 0) {
        std::cerr << "invalid replacement backspace count=" << backspaces << '\n';
        return 1;
    }

    if (!send(engine, entry, *context, FcitxKey_x, true) || !context->commits().empty()) {
        std::cerr << "buffered x produced immediate output; commits=" << context->commits().size() << '\n';
        return 1;
    }
    for (int i = 0; i < backspaces; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == backspaces))
            return 1;
    }

    int replayBackspaces = 0;
    if (!listener.receive(replayBackspaces) || replayBackspaces <= 0) {
        std::cerr << "buffered x did not start replacement; count=" << replayBackspaces << '\n';
        return 1;
    }
    for (int i = 0; i < replayBackspaces; ++i) {
        if (!send(engine, entry, *context, FcitxKey_BackSpace, i + 1 == replayBackspaces))
            return 1;
    }

    const std::vector<std::string> expected{"á", "ã"};
    if (context->commits() != expected) {
        std::cerr << "replay commits=";
        for (const auto& commit : context->commits())
            std::cerr << "['" << commit << "']";
        std::cerr << " expected=['á']['ã'] first-backspaces=" << backspaces << " replay-backspaces=" << replayBackspaces << '\n';
        return 1;
    }
    return 0;
}
