// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression tests for EmojiLoader search ranking and history robustness.
//
// Contract (defined by EmojiCoreFix, implemented in src/emoji.cpp):
// - search is case-insensitive (ASCII fold), empty query -> 0 results,
//   ranking tiers exact > prefix > word-boundary > fuzzy-subsequence
//   (tiers beat raw bonus points; score breaks ties within a tier),
//   stable order for equal scores, truncated to kMaxEmojiSearchResults.
// - history keeps newest kMaxEmojiHistory entries, dedups by emoji string
//   (newest wins), ignores malformed/empty lines, preserves UTF-8;
//   save is atomic (tmp + rename) and never destroys the previous file.
// Search tests need the fixed search (no addon early-out); history
// load-cap/dedupe/malformed tests fail against the old implementation.
#include "emoji.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const std::string& step, const std::string& expected, const std::string& actual) {
    if (ok) {
        return;
    }
    ++failures;
    std::cerr << "Step: " << step << "\nExpected: " << expected << "\nActual: " << actual << "\n";
}

std::string outputsStr(const std::vector<EmojiEntry>& entries) {
    std::string out = "[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += entries[i].output;
    }
    return out + "]";
}

bool hasOutput(const std::vector<EmojiEntry>& entries, const std::string& output) {
    for (const auto& e : entries) {
        if (e.output == output) {
            return true;
        }
    }
    return false;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

EmojiLoader makeSearchLoader(std::vector<EmojiEntry> entries) {
    EmojiLoader loader(nullptr);
    loader.setEmojiListForTest(std::move(entries));
    return loader;
}

} // namespace

int main() {
    // Sandbox user paths so the constructor's default history location never
    // touches the developer's real fcitx5 config (read-only anyway).
    const auto root = std::filesystem::temp_directory_path() / ("fcitx5-lotus-emoji-search-history-" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    setenv("HOME", root.c_str(), 1);
    setenv("XDG_CONFIG_HOME", root.c_str(), 1);
    setenv("XDG_DATA_HOME", root.c_str(), 1);
    setenv("XDG_CACHE_HOME", root.c_str(), 1);

    check(EmojiLoader::kMaxEmojiHistory == 9, "history cap constant", "9", std::to_string(EmojiLoader::kMaxEmojiHistory));

    // Search: empty query -> 0 results.
    {
        auto loader = makeSearchLoader({{.trigger = "cat", .output = "exact"}, {.trigger = "dog", .output = "dog"}});
        check(loader.search("").empty(), "empty query", "0 results", std::to_string(loader.search("").size()) + " results");
    }

    // Search: case-insensitive ('CAT' matches 'cat').
    {
        auto loader = makeSearchLoader({{.trigger = "cat", .output = "cat"}, {.trigger = "dog", .output = "dog"}});
        const auto upper = loader.search("CAT");
        const auto mixed = loader.search("Cat");
        check(hasOutput(upper, "cat"), "uppercase query", "contains cat", outputsStr(upper));
        check(hasOutput(mixed, "cat"), "mixed-case query", "contains cat", outputsStr(mixed));
    }

    // Search: exact 'cat' beats fuzzy-subsequence 'c_a_t'.
    {
        auto loader = makeSearchLoader({{.trigger = "c_a_t", .output = "fuzzy"}, {.trigger = "cat", .output = "exact"}});
        const auto results = loader.search("cat");
        check(!results.empty() && results[0].output == "exact", "exact beats fuzzy", "exact first", outputsStr(results));
    }

    // Search: exact 'cat' beats prefix 'category'.
    {
        auto loader = makeSearchLoader({{.trigger = "category", .output = "prefix"}, {.trigger = "cat", .output = "exact"}});
        const auto results = loader.search("cat");
        check(!results.empty() && results[0].output == "exact", "exact beats prefix", "exact first", outputsStr(results));
    }

    // Search: full tier order exact > prefix > word-boundary > fuzzy.
    {
        auto loader = makeSearchLoader({
            {.trigger = "c_a_t", .output = "fuzzy"},
            {.trigger = "my_cat", .output = "word"},
            {.trigger = "category", .output = "prefix"},
            {.trigger = "cat", .output = "exact"},
        });
        const auto results = loader.search("cat");
        const bool ordered = results.size() == 4 && results[0].output == "exact" && results[1].output == "prefix" && results[2].output == "word" &&
                             results[3].output == "fuzzy";
        check(ordered, "tier order", "[exact, prefix, word, fuzzy]", outputsStr(results));
    }

    // Search: stable order for equal scores (duplicate triggers keep insertion order).
    {
        auto loader = makeSearchLoader({
            {.trigger = "cat", .output = "first"},
            {.trigger = "cat", .output = "second"},
            {.trigger = "cat", .output = "third"},
        });
        const auto results = loader.search("cat");
        const bool stable = results.size() == 3 && results[0].output == "first" && results[1].output == "second" && results[2].output == "third";
        check(stable, "stable equal-score order", "[first, second, third]", outputsStr(results));
    }

    // Search: bounded Top-K.
    {
        std::vector<EmojiEntry> entries;
        for (size_t i = 0; i < EmojiLoader::kMaxEmojiSearchResults + 50; ++i) {
            entries.push_back({.trigger = "emoji" + std::to_string(i), .output = "e" + std::to_string(i)});
        }
        auto loader = makeSearchLoader(std::move(entries));
        const auto results = loader.search("emoji");
        check(results.size() == EmojiLoader::kMaxEmojiSearchResults, "top-k bound", std::to_string(EmojiLoader::kMaxEmojiSearchResults),
              std::to_string(results.size()));
    }

    // Search: 1-char query latency observation (bounded result keeps it cheap).
    {
        std::vector<EmojiEntry> entries;
        for (size_t i = 0; i < 5000; ++i) {
            entries.push_back({.trigger = "k" + std::to_string(i), .output = "e" + std::to_string(i)});
        }
        auto loader = makeSearchLoader(std::move(entries));
        const auto start = std::chrono::steady_clock::now();
        const auto results = loader.search("k");
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::cout << "1-char query over 5000 entries: " << ms << " ms, " << results.size() << " results\n";
        check(results.size() == EmojiLoader::kMaxEmojiSearchResults, "1-char query bounded", std::to_string(EmojiLoader::kMaxEmojiSearchResults),
              std::to_string(results.size()));
    }

    // History: load keeps newest 9 of 12 (file order is newest-first).
    {
        const auto path = root / "history-cap.conf";
        std::string text;
        for (int i = 0; i < 12; ++i) {
            text += "history" + std::to_string(i) + "=e" + std::to_string(i) + "\n";
        }
        writeText(path, text);
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        const auto& history = loader.history();
        const bool capped = history.size() == 9 && history.front().output == "e0" && history.back().output == "e8";
        check(capped, "load keeps newest 9", "9 entries e0..e8",
              std::to_string(history.size()) + " entries" + (history.empty() ? "" : " " + history.front().output + ".." + history.back().output));
    }

    // History: duplicates collapse to one (newest occurrence wins).
    {
        const auto path = root / "history-dupe.conf";
        writeText(path, "a=\xF0\x9F\x90\xB1\nb=\xF0\x9F\x90\xB6\nc=\xF0\x9F\x90\xB1\n");
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        const auto& history = loader.history();
        const bool deduped = history.size() == 2 && history[0].output == "\xF0\x9F\x90\xB1" && history[1].output == "\xF0\x9F\x90\xB6";
        check(deduped, "duplicates collapse", "2 entries, newest first", outputsStr(history));
    }

    // History: malformed lines ignored (no '=', empty lines, empty output).
    {
        const auto path = root / "history-malformed.conf";
        writeText(path, "junk-no-equals\n\nt=\nok=\xF0\x9F\x90\xB1\na=b=c\n");
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        const auto& history = loader.history();
        const bool clean = history.size() == 2 && history[0].output == "\xF0\x9F\x90\xB1" && history[1].output == "b=c";
        check(clean, "malformed ignored", "2 entries [cat-emoji, b=c]", outputsStr(history));
    }

    // History: UTF-8 round-trip through record + reload.
    {
        const auto path = root / "history-utf8.conf";
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        loader.recordHistory({.trigger = "caf\xC3\xA9", .output = "\xE2\x98\x95"});
        loader.recordHistory({.trigger = "cat", .output = "\xF0\x9F\x90\xB1"});
        EmojiLoader reloaded(nullptr);
        reloaded.setHistoryPathForTest(path.string());
        const auto& history = reloaded.history();
        const bool roundTripped = history.size() == 2 && history[0].output == "\xF0\x9F\x90\xB1" && history[0].trigger == "cat" &&
                                  history[1].output == "\xE2\x98\x95" && history[1].trigger == "caf\xC3\xA9";
        check(roundTripped, "utf-8 round-trip", "2 entries verbatim", outputsStr(history));
    }

    // History: no tmp leftover after successful save (atomic tmp + rename).
    {
        const auto path = root / "history-tmp.conf";
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        loader.recordHistory({.trigger = "cat", .output = "\xF0\x9F\x90\xB1"});
        check(!std::filesystem::exists(path.string() + ".tmp", ec), "no tmp leftover", "no .tmp file",
              std::filesystem::exists(path.string() + ".tmp", ec) ? ".tmp remains" : "no .tmp file");
    }

    // History: failed write preserves the previous file.
    {
        const auto dir = root / "history-readonly";
        std::filesystem::create_directories(dir, ec);
        const auto path = dir / "history.conf";
        EmojiLoader loader(nullptr);
        loader.setHistoryPathForTest(path.string());
        loader.recordHistory({.trigger = "cat", .output = "\xF0\x9F\x90\xB1"});
        const std::string before = readText(path);
        std::error_code permEc;
        std::filesystem::permissions(dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec, permEc);
        loader.recordHistory({.trigger = "dog", .output = "\xF0\x9F\x90\xB6"});
        std::filesystem::permissions(dir,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec,
                                     permEc);
        if (::geteuid() == 0) {
            std::cout << "SKIP failed-write-preserves (running as root, permissions do not block writes)\n";
        } else {
            check(readText(path) == before, "failed write preserves", "file unchanged", before == readText(path) ? "unchanged" : "modified");
        }
        check(!std::filesystem::exists(path.string() + ".tmp", ec), "no tmp leftover on failure", "no .tmp file",
              std::filesystem::exists(path.string() + ".tmp", ec) ? ".tmp remains" : "no .tmp file");
    }

    std::filesystem::remove_all(root, ec);
    if (failures > 0) {
        std::cerr << "FAILURES: " << failures << "\n";
        return 1;
    }
    std::cout << "emoji search/history: all checks passed\n";
    return 0;
}
