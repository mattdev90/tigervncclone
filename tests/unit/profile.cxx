#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtest/gtest.h>
#include <string>
#include <cstdio>

#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#endif

#include "parameters.h"

// --- getProfileFilename tests ---

TEST(ProfileFilename, BasicHostPort)
{
  EXPECT_EQ(getProfileFilename("192.168.1.50", 5900),
            "192.168.1.50_5900.tigervnc");
}

TEST(ProfileFilename, NonDefaultPort)
{
  EXPECT_EQ(getProfileFilename("myserver.local", 5901),
            "myserver.local_5901.tigervnc");
}

TEST(ProfileFilename, HostnameWithDots)
{
  EXPECT_EQ(getProfileFilename("office.internal.corp", 5900),
            "office.internal.corp_5900.tigervnc");
}

TEST(ProfileFilename, ColonsBecomesUnderscores)
{
  EXPECT_EQ(getProfileFilename("server:bad:chars", 5900),
            "server_bad_chars_5900.tigervnc");
}

TEST(ProfileFilename, IPv6Address)
{
  EXPECT_EQ(getProfileFilename("::1", 5900),
            "__1_5900.tigervnc");
}

// --- Password hex round-trip tests ---

TEST(PasswordHex, RoundTrip)
{
  std::string password = "secret";
  std::string hex = obfuscatedPasswordToHex(password);
  EXPECT_FALSE(hex.empty());
  EXPECT_EQ(hex.size() % 2, 0u);
  std::string recovered = hexToObfuscatedPassword(hex);
  EXPECT_EQ(recovered, password);
}

TEST(PasswordHex, EmptyPasswordGivesEmptyHex)
{
  std::string hex = obfuscatedPasswordToHex("");
  EXPECT_TRUE(hex.empty());
}

TEST(PasswordHex, EmptyHexGivesEmptyPassword)
{
  std::string recovered = hexToObfuscatedPassword("");
  EXPECT_TRUE(recovered.empty());
}

TEST(PasswordHex, OddLengthHexGivesEmpty)
{
  std::string recovered = hexToObfuscatedPassword("abc");
  EXPECT_TRUE(recovered.empty());
}

TEST(PasswordHex, AnotherRoundTrip)
{
  std::string password = "hunter2";
  std::string hex = obfuscatedPasswordToHex(password);
  std::string recovered = hexToObfuscatedPassword(hex);
  EXPECT_EQ(recovered, password);
}

// --- Profile I/O tests (POSIX only) ---

#ifndef _WIN32

class ProfileIOTest : public ::testing::Test {
protected:
  std::string tempDir;

  void SetUp() override {
    char tmpl[] = "/tmp/tigervnc_test_XXXXXX";
    char* result = mkdtemp(tmpl);
    ASSERT_NE(result, nullptr);
    tempDir = result;
  }

  void TearDown() override {
    DIR* d = opendir(tempDir.c_str());
    if (d) {
      struct dirent* entry;
      while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        remove((tempDir + "/" + name).c_str());
      }
      closedir(d);
    }
    rmdir(tempDir.c_str());
  }
};

TEST_F(ProfileIOTest, SaveAndLoadProfile)
{
  std::string filepath = tempDir + "/test_5900.tigervnc";
  saveProfileToFile(filepath.c_str(), "192.168.1.50:5900",
                    "Office PC", "mypassword");

  ProfileInfo info = loadProfile(filepath.c_str());
  EXPECT_EQ(info.serverName, "192.168.1.50:5900");
  EXPECT_EQ(info.profileName, "Office PC");
  EXPECT_EQ(info.filePath, filepath);
  EXPECT_TRUE(info.hasPassword);
}

TEST_F(ProfileIOTest, SaveWithoutPassword)
{
  std::string filepath = tempDir + "/test_5901.tigervnc";
  saveProfileToFile(filepath.c_str(), "10.0.0.5", "Home", nullptr);

  ProfileInfo info = loadProfile(filepath.c_str());
  EXPECT_EQ(info.serverName, "10.0.0.5");
  EXPECT_EQ(info.profileName, "Home");
  EXPECT_FALSE(info.hasPassword);
}

TEST_F(ProfileIOTest, SaveWithoutProfileName)
{
  std::string filepath = tempDir + "/test_5902.tigervnc";
  saveProfileToFile(filepath.c_str(), "10.0.0.5", nullptr, nullptr);

  ProfileInfo info = loadProfile(filepath.c_str());
  EXPECT_EQ(info.serverName, "10.0.0.5");
  EXPECT_TRUE(info.profileName.empty());
  EXPECT_FALSE(info.hasPassword);
}

TEST_F(ProfileIOTest, LoadPasswordFromProfile)
{
  std::string filepath = tempDir + "/test_5903.tigervnc";
  saveProfileToFile(filepath.c_str(), "192.168.1.50:5900",
                    "Office", "secret123");

  std::string password = loadPasswordFromProfile(filepath.c_str());
  EXPECT_EQ(password, "secret123");
}

TEST_F(ProfileIOTest, LoadPasswordFromProfileWithoutPassword)
{
  std::string filepath = tempDir + "/test_5904.tigervnc";
  saveProfileToFile(filepath.c_str(), "10.0.0.5", "Home", nullptr);

  std::string password = loadPasswordFromProfile(filepath.c_str());
  EXPECT_TRUE(password.empty());
}

TEST_F(ProfileIOTest, LoadAllProfilesFromDir)
{
  std::string fp1 = tempDir + "/a_5900.tigervnc";
  std::string fp2 = tempDir + "/b_5901.tigervnc";
  saveProfileToFile(fp1.c_str(), "1.2.3.4", "Server A", nullptr);
  saveProfileToFile(fp2.c_str(), "5.6.7.8", "Server B", "pw");

  ProfileInfo a = loadProfile(fp1.c_str());
  ProfileInfo b = loadProfile(fp2.c_str());
  EXPECT_EQ(a.serverName, "1.2.3.4");
  EXPECT_EQ(b.serverName, "5.6.7.8");
  EXPECT_TRUE(b.hasPassword);
}

TEST_F(ProfileIOTest, DeleteProfile)
{
  std::string filepath = tempDir + "/todelete_5900.tigervnc";
  saveProfileToFile(filepath.c_str(), "1.2.3.4", nullptr, nullptr);

  FILE* f = fopen(filepath.c_str(), "r");
  ASSERT_NE(f, nullptr);
  fclose(f);

  deleteProfile(filepath.c_str());

  f = fopen(filepath.c_str(), "r");
  EXPECT_EQ(f, nullptr);
}

#endif // not _WIN32
