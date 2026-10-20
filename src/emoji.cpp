/*
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "emoji.h"
#include "lotus-utils.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
 #include <emoji_public.h>
 #include <fcitx-utils/standardpath.h>
#include <fstream>
#include <string_view>
#include <utility>

EmojiLoader::EmojiLoader(fcitx::AddonManager* addonManager) {
    if (addonManager != nullptr) {
        emojiAddon_ = addonManager->addon("emoji", true);
    }
    historyPath_ = lotusConfigDir() + "/lotus-emoji-history.conf";
    loadFromFcitx5("en");
    loadHistory();
}

void EmojiLoader::loadFromFcitx5(const std::string& language) {
    emojiList.clear();
    if (emojiAddon_ == nullptr) {
        return;
    }

    emojiAddon_->call<fcitx::IEmoji::prefix>(language, "", true, [this](const std::string& key, const std::vector<std::string>& values) {
        for (const auto& emoji : values) {
            EmojiEntry entry;
            entry.output  = emoji;
            entry.trigger = key;
            emojiList.push_back(entry);
        }
        return true;
    });
}

void EmojiLoader::recordHistory(const EmojiEntry& entry) {
    // Remove if already exists to move to top
    historyList.erase(std::ranges::remove_if(historyList, [&](const auto& e) { return e.output == entry.output; }).begin(), historyList.end());

    historyList.insert(historyList.begin(), entry);

    // Keep only the newest entries.
    if (historyList.size() > kMaxEmojiHistory) {
        historyList.resize(kMaxEmojiHistory);
    }

    saveHistory();
}

void EmojiLoader::loadHistory() {
    historyList.clear();
    std::ifstream file(historyPath_);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        // Lines are "trigger=output"; anything else is malformed. Bytes
        // (including UTF-8) are preserved verbatim.
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string output = line.substr(pos + 1);
        if (output.empty()) {
            continue;
        }
        // File order is newest-first: keep the first entry per emoji.
        const bool seen = std::ranges::any_of(historyList, [&](const auto& e) { return e.output == output; });
        if (seen) {
            continue;
        }
        EmojiEntry entry;
        entry.trigger = line.substr(0, pos);
        entry.output  = std::move(output);
        historyList.push_back(std::move(entry));
        if (historyList.size() >= kMaxEmojiHistory) {
            break;
        }
    }
}

void EmojiLoader::saveHistory() {
    // Atomic write: a failed save must never destroy the previous history.
    const std::string tmpPath = historyPath_ + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::trunc);
        if (!file.is_open()) {
            return;
        }
        size_t written = 0;
        for (const auto& entry : historyList) {
            if (written >= kMaxEmojiHistory) {
                break;
            }
            if (entry.output.empty()) {
                continue;
            }
            file << entry.trigger << "=" << entry.output << "\n";
            if (!file) {
                file.close();
                std::remove(tmpPath.c_str());
                return;
            }
            ++written;
        }
        file.flush();
        file.close();
        if (!file) {
            std::remove(tmpPath.c_str());
            return;
        }
    }
    // Durability nit (fsync) intentionally skipped: the rename is what makes
    // the save atomic; a crash may lose the latest write but never the file.
    if (std::rename(tmpPath.c_str(), historyPath_.c_str()) != 0) {
        std::remove(tmpPath.c_str());
    }
}

namespace {

    std::string asciiLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool isWordSeparator(char c) {
        return c == '_' || c == '-' || c == ' ' || c == '/';
    }

} // namespace

std::vector<EmojiEntry> EmojiLoader::search(const std::string& prefix) {
    if (prefix.empty()) {
        return {};
    }
    // Case-insensitive matching: fold the query and each trigger to lowercase
    // (ASCII minimum); the stored trigger/output bytes are never modified.
    const std::string query = asciiLower(prefix);

    // Tiers beat raw bonus points; the score only breaks ties within a tier.
    enum class Rank : int { Exact = 0, Prefix = 1, WordBoundary = 2, Fuzzy = 3 };
    struct Scored {
        EmojiEntry entry;
        Rank       rank;
        int        score;
    };
    std::vector<Scored> scored;
    scored.reserve(emojiList.size());

    for (const auto& entry : emojiList) {
        // Fast path: triggers are almost always already lowercase, so only
        // fold (allocating) when the trigger contains uppercase ASCII.
        std::string     lowered;
        std::string_view trigger = entry.trigger;
        for (char c : entry.trigger) {
            if (c >= 'A' && c <= 'Z') {
                lowered = asciiLower(entry.trigger);
                trigger = lowered;
                break;
            }
        }
        if (query.size() > trigger.size()) {
            continue;
        }

        Rank rank = Rank::Fuzzy;
        if (trigger == query) {
            rank = Rank::Exact;
        } else if (trigger.starts_with(query)) {
            rank = Rank::Prefix;
        } else {
            bool atBoundary = false;
            for (size_t i = 1; i + query.size() <= trigger.size(); ++i) {
                if (!isWordSeparator(trigger[i - 1])) {
                    continue;
                }
                if (trigger.compare(i, query.size(), query) == 0) {
                    atBoundary = true;
                    break;
                }
            }
            if (atBoundary) {
                rank = Rank::WordBoundary;
            } else {
                // Fuzzy: the query must be a subsequence of the trigger.
                size_t queryIndex = 0;
                for (char c : trigger) {
                    if (c == query[queryIndex] && ++queryIndex == query.size()) {
                        break;
                    }
                }
                if (queryIndex != query.size()) {
                    continue;
                }
                rank = Rank::Fuzzy;
            }
        }

        // Within-tier score: consecutive/boundary/start bonuses, penalized by
        // trigger length and match span (shorter, tighter matches first).
        int    score              = 0;
        size_t queryIndex         = 0;
        int    lastMatchIndex     = -1;
        int    consecutiveMatches = 0;
        int    firstMatchIndex    = -1;
        for (size_t i = 0; i < trigger.size() && queryIndex < query.size(); ++i) {
            if (trigger[i] == query[queryIndex]) {
                if (queryIndex == 0)
                    firstMatchIndex = static_cast<int>(i); // NOLINT

                if (lastMatchIndex != -1 && static_cast<int>(i) == lastMatchIndex + 1) {
                    ++consecutiveMatches;
                    score += (20 * consecutiveMatches);
                } else {
                    consecutiveMatches = 0;
                }

                if (i == 0 || isWordSeparator(trigger[i - 1])) {
                    score += 50;
                }

                lastMatchIndex = static_cast<int>(i); // NOLINT
                ++queryIndex;
            }
        }
        if (queryIndex != query.size()) {
            continue;
        }
        if (firstMatchIndex == 0)
            score += 100;
        score -= static_cast<int>(trigger.size());
        score -= (lastMatchIndex - firstMatchIndex);

        scored.push_back({.entry = entry, .rank = rank, .score = score});
    }

    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.score > b.score;
    });
    if (scored.size() > kMaxEmojiSearchResults) {
        scored.resize(kMaxEmojiSearchResults);
    }

    std::vector<EmojiEntry> finalResults;
    finalResults.reserve(scored.size());
    for (const auto& result : scored) {
        finalResults.push_back(result.entry);
    }
    return finalResults;
}

size_t EmojiLoader::size() const {
    return emojiList.size();
}

void EmojiLoader::setEmojiListForTest(std::vector<EmojiEntry> entries) {
    emojiList = std::move(entries);
}

void EmojiLoader::setHistoryPathForTest(const std::string& path) {
    historyPath_ = path;
    loadHistory();
}