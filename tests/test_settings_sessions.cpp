// Session (tab) persistence in settings.cfg — the "reopen last session on
// launch" list. These guard the SHRINK case: the writer preserves keys it
// didn't emit (so another build's settings round-trip instead of vanishing),
// which for an INDEXED LIST silently resurrects removed entries.
#include "io/Settings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using materializr::AppSettings;
namespace SettingsIO = materializr::SettingsIO;

namespace {
std::string tmpCfg(const char* tag) {
    static int n = 0;
    return (fs::temp_directory_path() /
            ("mzr_settings_" + std::string(tag) + "_" + std::to_string(++n) +
             ".cfg")).string();
}
std::string readAll(const std::string& p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}
} // namespace

TEST(SettingsSessions, RoundTripsOpenTabsAndActiveIndex) {
    const std::string p = tmpCfg("roundtrip");
    AppSettings s;
    s.autoOpenLastProject = true;
    s.sessionPaths = {"/tmp/a.mzr", "/tmp/b.mzr", "/tmp/c.mzr"};
    s.sessionActive = 2;
    ASSERT_TRUE(SettingsIO::save(p, s));

    AppSettings r = SettingsIO::load(p);
    EXPECT_TRUE(r.autoOpenLastProject);
    ASSERT_EQ(r.sessionPaths.size(), 3u);
    EXPECT_EQ(r.sessionPaths[0], "/tmp/a.mzr");
    EXPECT_EQ(r.sessionPaths[2], "/tmp/c.mzr");
    EXPECT_EQ(r.sessionActive, 2);
    fs::remove(p);
}

// An UNSAVED tab contributes an empty entry so the indices stay aligned with
// sessionActive; it must survive the round trip as a placeholder rather than
// truncating the list (the recents reader stops at the first empty — this one
// must not).
TEST(SettingsSessions, EmptyPlaceholderKeepsIndicesAligned) {
    const std::string p = tmpCfg("placeholder");
    AppSettings s;
    s.sessionPaths = {"", "/tmp/real.mzr"};
    s.sessionActive = 1;
    ASSERT_TRUE(SettingsIO::save(p, s));

    AppSettings r = SettingsIO::load(p);
    ASSERT_EQ(r.sessionPaths.size(), 2u);
    EXPECT_EQ(r.sessionPaths[0], "");
    EXPECT_EQ(r.sessionPaths[1], "/tmp/real.mzr");
    EXPECT_EQ(r.sessionActive, 1);
    fs::remove(p);
}

// THE REGRESSION: save 3 tabs, then save 1 over the same file. The writer's
// "preserve keys another build wrote" pass must NOT carry session1/session2
// forward — doing so reopened tabs the user had closed.
TEST(SettingsSessions, ClosingTabsDoesNotResurrectThem) {
    const std::string p = tmpCfg("shrink");
    AppSettings three;
    three.sessionPaths = {"/tmp/a.mzr", "/tmp/b.mzr", "/tmp/c.mzr"};
    three.sessionActive = 2;
    ASSERT_TRUE(SettingsIO::save(p, three));

    AppSettings one;
    one.sessionPaths = {"/tmp/a.mzr"};
    one.sessionActive = 0;
    ASSERT_TRUE(SettingsIO::save(p, one));

    const std::string text = readAll(p);
    EXPECT_EQ(text.find("session1_path"), std::string::npos)
        << "a closed tab was preserved into the next launch:\n" << text;
    EXPECT_EQ(text.find("session2_path"), std::string::npos);

    AppSettings r = SettingsIO::load(p);
    ASSERT_EQ(r.sessionPaths.size(), 1u);
    EXPECT_EQ(r.sessionPaths[0], "/tmp/a.mzr");
    fs::remove(p);
}

// Same shrink hazard for the recents list (its reader stops at the first
// missing key, which masked this — but a stale CONTIGUOUS key would not be).
TEST(SettingsSessions, ShrinkingRecentsDoesNotResurrectEntries) {
    const std::string p = tmpCfg("recents");
    AppSettings many;
    many.recentProjects = {{"/tmp/a.mzr", "a"}, {"/tmp/b.mzr", "b"},
                           {"/tmp/c.mzr", "c"}};
    ASSERT_TRUE(SettingsIO::save(p, many));

    AppSettings few;
    few.recentProjects = {{"/tmp/a.mzr", "a"}};
    ASSERT_TRUE(SettingsIO::save(p, few));

    const std::string text = readAll(p);
    EXPECT_EQ(text.find("recent1_ref"), std::string::npos) << text;

    AppSettings r = SettingsIO::load(p);
    ASSERT_EQ(r.recentProjects.size(), 1u);
    EXPECT_EQ(r.recentProjects[0].ref, "/tmp/a.mzr");
    fs::remove(p);
}

// Genuinely-unknown keys (a newer build's setting) must still round-trip —
// the shrink fix must not have thrown that away.
TEST(SettingsSessions, UnknownKeysStillPreserved) {
    const std::string p = tmpCfg("unknown");
    {
        std::ofstream f(p);
        f << "someFutureSetting = 42\ntheme = 1\n";
    }
    AppSettings s = SettingsIO::load(p);
    ASSERT_TRUE(SettingsIO::save(p, s));
    EXPECT_NE(readAll(p).find("someFutureSetting = 42"), std::string::npos)
        << readAll(p);
    fs::remove(p);
}
