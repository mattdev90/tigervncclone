# Profile-Based Server Dialog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the TigerVNC connection dialog with a profile-based system that auto-saves connections, persists passwords, and enables one-click connect from a profile list.

**Architecture:** Extend the existing `.tigervnc` file format with `ProfileName` and `Password` fields. Store profiles as individual files in a `profiles/` subdirectory of the VNC config dir. Rebuild `ServerDialog` to show an `Fl_Hold_Browser` listing saved profiles. Update `AuthDialog` to offer persistent password saving. Remove the old server history system.

**Tech Stack:** C++, FLTK (Fl_Hold_Browser, Fl_Input, Fl_Window), Google Test, rfb::obfuscate/deobfuscate for password storage.

**Spec:** `docs/superpowers/specs/2026-03-27-profile-based-server-dialog-design.md`

---

## File Structure

| File | Role |
|---|---|
| `vncviewer/parameters.h` | Declare profile helpers: `getProfilesDir()`, `getProfileFilename()`, `saveProfile()`, `loadProfile()`, `loadAllProfiles()`, `deleteProfile()`, `ProfileInfo` struct |
| `vncviewer/parameters.cxx` | Implement profile I/O: directory management, password hex encode/decode, profile scan, save/load with Password and ProfileName fields |
| `vncviewer/ServerDialog.h` | Rebuilt class: `Fl_Hold_Browser*` profile list, `Fl_Input*` host input, profile management handlers |
| `vncviewer/ServerDialog.cxx` | Full rewrite: profile list UI, double-click connect, host filtering, edit/delete actions |
| `vncviewer/AuthDialog.h` | Rename checkbox, always show when password needed |
| `vncviewer/AuthDialog.cxx` | Change label to "Save password", remove `reconnectOnError` gate |
| `vncviewer/CConn.h` | Remove `savedUsername`/`savedPassword` statics, add `activeProfilePath` |
| `vncviewer/CConn.cxx` | Use profile-based password persistence instead of in-memory statics |
| `vncviewer/vncviewer.cxx` | Update startup: create profiles dir, migrate old history, load profiles |
| `tests/unit/profile.cxx` | Unit tests for profile filename generation, password hex encoding, profile I/O |
| `tests/unit/CMakeLists.txt` | Add profile test executable |

---

### Task 1: Profile Data Structures and Filename Utility

**Files:**
- Modify: `vncviewer/parameters.h`
- Modify: `vncviewer/parameters.cxx`
- Create: `tests/unit/profile.cxx`
- Modify: `tests/unit/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for profile filename generation**

In `tests/unit/profile.cxx`:

```cpp
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtest/gtest.h>

// We test the standalone utility functions from parameters.h
#include "parameters.h"

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

TEST(ProfileFilename, InvalidFilenameChars)
{
  EXPECT_EQ(getProfileFilename("server:with:colons", 5900),
            "server_with_colons_5900.tigervnc");
}

TEST(ProfileFilename, IPv6Address)
{
  EXPECT_EQ(getProfileFilename("::1", 5900),
            "__1_5900.tigervnc");
}
```

- [ ] **Step 2: Write failing tests for password hex encode/decode**

Append to `tests/unit/profile.cxx`:

```cpp
TEST(PasswordHex, RoundTrip)
{
  std::string password = "secret";
  std::string hex = obfuscatedPasswordToHex(password);
  EXPECT_FALSE(hex.empty());
  // Hex string should be even length (two chars per byte)
  EXPECT_EQ(hex.size() % 2, 0u);

  std::string recovered = hexToObfuscatedPassword(hex);
  EXPECT_EQ(recovered, password);
}

TEST(PasswordHex, EmptyPassword)
{
  std::string hex = obfuscatedPasswordToHex("");
  EXPECT_TRUE(hex.empty());

  std::string recovered = hexToObfuscatedPassword("");
  EXPECT_TRUE(recovered.empty());
}

TEST(PasswordHex, InvalidHex)
{
  // Odd-length hex string should return empty
  std::string recovered = hexToObfuscatedPassword("abc");
  EXPECT_TRUE(recovered.empty());
}
```

- [ ] **Step 3: Add test to CMakeLists.txt**

In `tests/unit/CMakeLists.txt`, append:

```cmake
add_executable(profile profile.cxx ../../vncviewer/parameters.cxx)
target_include_directories(profile SYSTEM PUBLIC ${FLTK_INCLUDE_DIR} ${Intl_INCLUDE_DIR})
target_link_libraries(profile core rfb ${FLTK_LIBRARIES} ${Intl_LIBRARIES} GTest::gtest_main)
gtest_discover_tests(profile)
```

- [ ] **Step 4: Add ProfileInfo struct and function declarations to parameters.h**

In `vncviewer/parameters.h`, add after the `#ifdef _WIN32` block (before the closing `#endif` of the header guard):

```cpp
#include <string>
#include <vector>

struct ProfileInfo {
  std::string serverName;
  std::string profileName;    // User-friendly nickname, empty = use serverName
  std::string filePath;       // Full path to .tigervnc file
  bool hasPassword;           // Whether a saved password exists
};

std::string getProfilesDir();
std::string getProfileFilename(const char* host, int port);

// Password hex encoding (obfuscate → hex string for .tigervnc files)
std::string obfuscatedPasswordToHex(const std::string& password);
std::string hexToObfuscatedPassword(const std::string& hex);

// Profile I/O
void saveProfile(const char* servername, const char* profileName,
                 const char* password);
ProfileInfo loadProfile(const char* filepath);
std::vector<ProfileInfo> loadAllProfiles();
void deleteProfile(const char* filepath);
```

- [ ] **Step 5: Implement getProfileFilename()**

In `vncviewer/parameters.cxx`, add near the top (after the existing includes):

```cpp
#include <rfb/obfuscate.h>
#include <sys/stat.h>
```

Then add the implementation (before `saveViewerParameters`):

```cpp
std::string getProfileFilename(const char* host, int port)
{
  std::string filename(host);

  // Replace characters that are invalid in filenames
  for (char& c : filename) {
    if (c == ':' || c == '/' || c == '\\' || c == '*' ||
        c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      c = '_';
  }

  filename += "_" + std::to_string(port) + ".tigervnc";
  return filename;
}
```

- [ ] **Step 6: Implement password hex encode/decode**

In `vncviewer/parameters.cxx`, add after `getProfileFilename()`:

```cpp
std::string obfuscatedPasswordToHex(const std::string& password)
{
  if (password.empty())
    return "";

  std::vector<uint8_t> obfPwd = rfb::obfuscate(password.c_str());

  std::string hex;
  hex.reserve(obfPwd.size() * 2);
  for (uint8_t byte : obfPwd) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", byte);
    hex += buf;
  }
  return hex;
}

std::string hexToObfuscatedPassword(const std::string& hex)
{
  if (hex.empty())
    return "";

  if (hex.size() % 2 != 0)
    return "";

  std::vector<uint8_t> obfPwd;
  obfPwd.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    unsigned int byte;
    if (sscanf(hex.c_str() + i, "%02x", &byte) != 1)
      return "";
    obfPwd.push_back(static_cast<uint8_t>(byte));
  }

  try {
    return rfb::deobfuscate(obfPwd.data(), obfPwd.size());
  } catch (std::exception&) {
    return "";
  }
}
```

- [ ] **Step 7: Implement getProfilesDir()**

In `vncviewer/parameters.cxx`, add after the hex functions:

```cpp
std::string getProfilesDir()
{
  const char* configDir = core::getvncconfigdir();
  if (configDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC config directory path"));

  std::string profilesDir = std::string(configDir) + "/profiles";
  return profilesDir;
}
```

- [ ] **Step 8: Build and run tests**

Run:
```bash
cd build && cmake .. && cmake --build . --target profile && ctest -R profile -V
```
Expected: All `ProfileFilename` and `PasswordHex` tests pass.

- [ ] **Step 9: Commit**

```bash
git add vncviewer/parameters.h vncviewer/parameters.cxx tests/unit/profile.cxx tests/unit/CMakeLists.txt
git commit -m "feat: add profile filename generation and password hex encoding utilities"
```

---

### Task 2: Profile Save and Load

**Files:**
- Modify: `vncviewer/parameters.h`
- Modify: `vncviewer/parameters.cxx`
- Modify: `tests/unit/profile.cxx`

- [ ] **Step 1: Write failing tests for profile save/load round-trip**

Append to `tests/unit/profile.cxx`:

```cpp
#include <cstdio>
#include <fstream>

#include <core/xdgdirs.h>

class ProfileIOTest : public ::testing::Test {
protected:
  std::string tempDir;

  void SetUp() override {
    // Create a temp directory for profiles
    char tmpl[] = "/tmp/tigervnc_test_XXXXXX";
    char* result = mkdtemp(tmpl);
    ASSERT_NE(result, nullptr);
    tempDir = result;
  }

  void TearDown() override {
    // Clean up
    std::string cmd = "rm -rf " + tempDir;
    system(cmd.c_str());
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
  std::string filepath = tempDir + "/test_5900.tigervnc";

  saveProfileToFile(filepath.c_str(), "10.0.0.5", "Home", nullptr);

  ProfileInfo info = loadProfile(filepath.c_str());

  EXPECT_EQ(info.serverName, "10.0.0.5");
  EXPECT_EQ(info.profileName, "Home");
  EXPECT_FALSE(info.hasPassword);
}

TEST_F(ProfileIOTest, SaveWithoutProfileName)
{
  std::string filepath = tempDir + "/test_5900.tigervnc";

  saveProfileToFile(filepath.c_str(), "10.0.0.5", nullptr, nullptr);

  ProfileInfo info = loadProfile(filepath.c_str());

  EXPECT_EQ(info.serverName, "10.0.0.5");
  EXPECT_TRUE(info.profileName.empty());
  EXPECT_FALSE(info.hasPassword);
}

TEST_F(ProfileIOTest, LoadPasswordFromProfile)
{
  std::string filepath = tempDir + "/test_5900.tigervnc";

  saveProfileToFile(filepath.c_str(), "192.168.1.50:5900",
                    "Office", "secret123");

  std::string password = loadPasswordFromProfile(filepath.c_str());
  EXPECT_EQ(password, "secret123");
}

TEST_F(ProfileIOTest, LoadPasswordFromProfileWithout)
{
  std::string filepath = tempDir + "/test_5900.tigervnc";

  saveProfileToFile(filepath.c_str(), "10.0.0.5", "Home", nullptr);

  std::string password = loadPasswordFromProfile(filepath.c_str());
  EXPECT_TRUE(password.empty());
}
```

- [ ] **Step 2: Add new declarations to parameters.h**

Add to the profile section in `vncviewer/parameters.h`:

```cpp
// Low-level profile I/O (for testing and direct use)
void saveProfileToFile(const char* filepath, const char* servername,
                       const char* profileName, const char* password);
std::string loadPasswordFromProfile(const char* filepath);
```

- [ ] **Step 3: Implement saveProfileToFile()**

In `vncviewer/parameters.cxx`, add after `getProfilesDir()`:

```cpp
void saveProfileToFile(const char* filepath, const char* servername,
                       const char* profileName, const char* password)
{
  FILE* f = fopen(filepath, "w+");
  if (!f)
    throw core::posix_error(
      core::format(_("Could not open \"%s\""), filepath), errno);

  const size_t buffersize = 256;
  char encodingBuffer[buffersize];

  fprintf(f, "%s\n", IDENTIFIER_STRING);
  fprintf(f, "\n");

  if (!encodeValue(servername, encodingBuffer, buffersize)) {
    fclose(f);
    throw std::runtime_error(
      core::format(_("Failed to save \"%s\": %s"), "ServerName",
                   _("Could not encode parameter")));
  }
  fprintf(f, "ServerName=%s\n", encodingBuffer);

  if (profileName != nullptr && profileName[0] != '\0') {
    if (!encodeValue(profileName, encodingBuffer, buffersize)) {
      fclose(f);
      throw std::runtime_error(
        core::format(_("Failed to save \"%s\": %s"), "ProfileName",
                     _("Could not encode parameter")));
    }
    fprintf(f, "ProfileName=%s\n", encodingBuffer);
  }

  if (password != nullptr && password[0] != '\0') {
    std::string hex = obfuscatedPasswordToHex(password);
    fprintf(f, "Password=%s\n", hex.c_str());
  }

  for (core::VoidParameter* param : parameterArray) {
    if (param->isDefault())
      continue;
    if (!encodeValue(param->getValueStr().c_str(),
                     encodingBuffer, buffersize)) {
      fclose(f);
      throw std::runtime_error(
        core::format(_("Failed to save \"%s\": %s"), param->getName(),
                     _("Could not encode parameter")));
    }
    fprintf(f, "%s=%s\n", param->getName(), encodingBuffer);
  }

  fclose(f);
}
```

- [ ] **Step 4: Implement loadProfile()**

In `vncviewer/parameters.cxx`, add after `saveProfileToFile()`:

```cpp
ProfileInfo loadProfile(const char* filepath)
{
  ProfileInfo info;
  info.filePath = filepath;
  info.hasPassword = false;

  FILE* f = fopen(filepath, "r");
  if (!f)
    throw core::posix_error(
      core::format(_("Could not open \"%s\""), filepath), errno);

  const size_t buffersize = 256;
  char line[buffersize];
  char decodingBuffer[buffersize];
  int lineNr = 0;

  while (!feof(f)) {
    lineNr++;
    if (!fgets(line, sizeof(line), f)) {
      if (feof(f))
        break;
      fclose(f);
      throw core::posix_error(
        core::format(_("Failed to read line %d in file \"%s\""),
                     lineNr, filepath), errno);
    }

    // First line is identifier
    if (lineNr == 1) {
      if (strncmp(line, IDENTIFIER_STRING, strlen(IDENTIFIER_STRING)) == 0)
        continue;
      fclose(f);
      throw std::runtime_error(core::format(
        _("Configuration file %s is in an invalid format"), filepath));
    }

    // Skip empty lines and comments
    if ((line[0] == '\n') || (line[0] == '#') || (line[0] == '\r'))
      continue;

    int len = strlen(line);
    if (line[len-1] == '\n') { line[len-1] = '\0'; len--; }
    if (len > 0 && line[len-1] == '\r') { line[len-1] = '\0'; len--; }
    if (len == 0) continue;

    char* value = strchr(line, '=');
    if (value == nullptr) continue;
    *value = '\0';
    value++;

    if (strcasecmp(line, "ServerName") == 0) {
      if (decodeValue(value, decodingBuffer, sizeof(decodingBuffer)))
        info.serverName = decodingBuffer;
    } else if (strcasecmp(line, "ProfileName") == 0) {
      if (decodeValue(value, decodingBuffer, sizeof(decodingBuffer)))
        info.profileName = decodingBuffer;
    } else if (strcasecmp(line, "Password") == 0) {
      info.hasPassword = (strlen(value) > 0);
    }
  }

  fclose(f);
  return info;
}
```

- [ ] **Step 5: Implement loadPasswordFromProfile()**

In `vncviewer/parameters.cxx`, add after `loadProfile()`:

```cpp
std::string loadPasswordFromProfile(const char* filepath)
{
  FILE* f = fopen(filepath, "r");
  if (!f)
    return "";

  const size_t buffersize = 256;
  char line[buffersize];

  while (!feof(f)) {
    if (!fgets(line, sizeof(line), f)) break;

    int len = strlen(line);
    if (line[len-1] == '\n') { line[len-1] = '\0'; len--; }
    if (len > 0 && line[len-1] == '\r') { line[len-1] = '\0'; len--; }

    char* value = strchr(line, '=');
    if (value == nullptr) continue;
    *value = '\0';
    value++;

    if (strcasecmp(line, "Password") == 0) {
      fclose(f);
      return hexToObfuscatedPassword(value);
    }
  }

  fclose(f);
  return "";
}
```

- [ ] **Step 6: Implement loadAllProfiles() and deleteProfile()**

In `vncviewer/parameters.cxx`, add after `loadPasswordFromProfile()`:

```cpp
std::vector<ProfileInfo> loadAllProfiles()
{
  std::vector<ProfileInfo> profiles;
  std::string dir = getProfilesDir();

  // Platform-specific directory listing
#ifdef _WIN32
  std::string pattern = dir + "\\*.tigervnc";
  WIN32_FIND_DATAW findData;
  wchar_t wpattern[PATH_MAX];
  fl_utf8towc(pattern.c_str(), pattern.size()+1, wpattern, PATH_MAX);

  HANDLE hFind = FindFirstFileW(wpattern, &findData);
  if (hFind == INVALID_HANDLE_VALUE)
    return profiles;

  do {
    char filename[PATH_MAX];
    fl_utf8fromwc(filename, PATH_MAX, findData.cFileName,
                  wcslen(findData.cFileName)+1);
    std::string filepath = dir + "\\" + filename;
    try {
      profiles.push_back(loadProfile(filepath.c_str()));
    } catch (std::exception& e) {
      vlog.error(_("Failed to load profile \"%s\": %s"),
                 filepath.c_str(), e.what());
    }
  } while (FindNextFileW(hFind, &findData));

  FindClose(hFind);
#else
  DIR* d = opendir(dir.c_str());
  if (d == nullptr)
    return profiles;

  struct dirent* entry;
  while ((entry = readdir(d)) != nullptr) {
    std::string name = entry->d_name;
    if (name.size() < 10 ||
        name.substr(name.size() - 10) != ".tigervnc")
      continue;

    std::string filepath = dir + "/" + name;
    try {
      profiles.push_back(loadProfile(filepath.c_str()));
    } catch (std::exception& e) {
      vlog.error(_("Failed to load profile \"%s\": %s"),
                 filepath.c_str(), e.what());
    }
  }

  closedir(d);
#endif

  return profiles;
}

void deleteProfile(const char* filepath)
{
  if (remove(filepath) != 0)
    throw core::posix_error(
      core::format(_("Could not delete \"%s\""), filepath), errno);
}
```

Add the necessary includes near the top of `parameters.cxx`:

```cpp
#ifndef _WIN32
#include <dirent.h>
#endif
```

- [ ] **Step 7: Implement saveProfile() convenience function**

In `vncviewer/parameters.cxx`, add after `deleteProfile()`:

```cpp
void saveProfile(const char* servername, const char* profileName,
                 const char* password)
{
  std::string host;
  int port;

  try {
    network::getHostAndPort(servername, &host, &port);
  } catch (std::exception&) {
    host = servername;
    port = 5900;
  }

  std::string dir = getProfilesDir();

  // Create profiles directory if it doesn't exist
#ifdef _WIN32
  wchar_t wdir[PATH_MAX];
  fl_utf8towc(dir.c_str(), dir.size()+1, wdir, PATH_MAX);
  CreateDirectoryW(wdir, nullptr);
#else
  mkdir(dir.c_str(), 0755);
#endif

  std::string filename = getProfileFilename(host.c_str(), port);
  std::string filepath = dir + "/" + filename;

  saveProfileToFile(filepath.c_str(), servername, profileName, password);
}
```

Add include at top of `parameters.cxx`:

```cpp
#include <network/TcpSocket.h>
```

- [ ] **Step 8: Build and run tests**

Run:
```bash
cd build && cmake .. && cmake --build . --target profile && ctest -R profile -V
```
Expected: All profile tests pass.

- [ ] **Step 9: Commit**

```bash
git add vncviewer/parameters.h vncviewer/parameters.cxx tests/unit/profile.cxx
git commit -m "feat: implement profile save, load, scan, and delete"
```

---

### Task 3: Update AuthDialog — Save Password Checkbox

**Files:**
- Modify: `vncviewer/AuthDialog.h`
- Modify: `vncviewer/AuthDialog.cxx`

- [ ] **Step 1: Update AuthDialog.h**

In `vncviewer/AuthDialog.h`, rename the method and member:

Change line 43:
```cpp
  bool getSavePassword();
```

Change line 49:
```cpp
  Fl_Check_Button* savePasswdCheckbox;
```

- [ ] **Step 2: Update AuthDialog.cxx — checkbox label and gate**

In `vncviewer/AuthDialog.cxx`, replace the checkbox creation block (lines 102-113):

```cpp
    savePasswdCheckbox = new Fl_Check_Button(LBLRIGHT(x, y,
                                                      CHECK_MIN_WIDTH,
                                                      CHECK_HEIGHT,
                                                      _("Save password")));
    y += CHECK_HEIGHT + INNER_MARGIN;
```

Remove the `if (reconnectOnError)` / `else` gate — the checkbox always appears when password is needed. The full replacement for lines 102-114:

```cpp
  if (needsPassword) {
    y += INPUT_LABEL_OFFSET;
    passwd = new Fl_Secret_Input(x, y,  w()- x - OUTER_MARGIN,
                                INPUT_HEIGHT, _("Password:"));
    passwd->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);
    y += INPUT_HEIGHT + INNER_MARGIN;

    savePasswdCheckbox = new Fl_Check_Button(LBLRIGHT(x, y,
                                                      CHECK_MIN_WIDTH,
                                                      CHECK_HEIGHT,
                                                      _("Save password")));
    y += CHECK_HEIGHT + INNER_MARGIN;
  } else {
    passwd = nullptr;
    savePasswdCheckbox = nullptr;
  }
```

Wait — this replaces the entire `if (needsPassword)` block (lines 95-114). Read carefully: the original has the password input creation inside this block too, so we replace the whole thing.

- [ ] **Step 3: Update getSavePassword() method**

In `vncviewer/AuthDialog.cxx`, replace the `getKeepPassword()` method (lines 163-167):

```cpp
bool AuthDialog::getSavePassword()
{
  if (savePasswdCheckbox)
    return savePasswdCheckbox->value();
  return false;
}
```

- [ ] **Step 4: Build to verify compilation**

Run:
```bash
cd build && cmake --build . --target vncviewer 2>&1 | head -30
```

This will fail because `CConn.cxx` still calls `getKeepPassword()` — that's expected and fixed in Task 4.

- [ ] **Step 5: Commit**

```bash
git add vncviewer/AuthDialog.h vncviewer/AuthDialog.cxx
git commit -m "feat: change AuthDialog checkbox from 'Keep for reconnect' to 'Save password'"
```

---

### Task 4: Update CConn — Profile-Based Password Persistence

**Files:**
- Modify: `vncviewer/CConn.h`
- Modify: `vncviewer/CConn.cxx`

- [ ] **Step 1: Update CConn.h**

Remove the static saved credential variables. Replace lines 120-121:

```cpp
  static std::string savedUsername;
  static std::string savedPassword;
```

With:

```cpp
  static std::string activeProfilePath;
```

- [ ] **Step 2: Update CConn.cxx — static variable definitions**

Replace lines 72-73:

```cpp
std::string CConn::savedUsername;
std::string CConn::savedPassword;
```

With:

```cpp
std::string CConn::activeProfilePath;
```

- [ ] **Step 3: Update CConn.cxx — clear credentials on auth error**

Replace lines 307-308:

```cpp
    savedUsername.clear();
    savedPassword.clear();
```

With:

```cpp
    activeProfilePath.clear();
```

- [ ] **Step 4: Update CConn.cxx — getUserPasswd method**

Replace the saved credential check block (lines 358-367):

```cpp
  if (user && !savedUsername.empty() && !savedPassword.empty()) {
    *user = savedUsername;
    *password = savedPassword;
    return;
  }

  if (!user && !savedPassword.empty()) {
    *password = savedPassword;
    return;
  }
```

With:

```cpp
  // Try loading password from active profile
  if (!activeProfilePath.empty()) {
    std::string savedPwd = loadPasswordFromProfile(activeProfilePath.c_str());
    if (!savedPwd.empty()) {
      if (password)
        *password = savedPwd;
      return;
    }
  }
```

- [ ] **Step 5: Update CConn.cxx — post-auth credential save**

Replace the post-auth block (lines 391-406):

```cpp
  if (ret_val == 1) {
    bool keepPasswd;

    if (reconnectOnError)
      keepPasswd = d.getKeepPassword();
    else
      keepPasswd = false;

    if (user) {
      *user = d.getUser();
      if (keepPasswd)
        savedUsername = d.getUser();
    }
    *password = d.getPassword();
    if (keepPasswd)
      savedPassword = d.getPassword();
  }
```

With:

```cpp
  if (ret_val == 1) {
    bool wantSave = d.getSavePassword();

    if (user)
      *user = d.getUser();
    *password = d.getPassword();

    if (wantSave && !activeProfilePath.empty()) {
      try {
        // Load existing profile info, re-save with password
        ProfileInfo info = loadProfile(activeProfilePath.c_str());
        saveProfileToFile(activeProfilePath.c_str(),
                          info.serverName.c_str(),
                          info.profileName.empty() ? nullptr : info.profileName.c_str(),
                          password->c_str());
      } catch (std::exception& e) {
        vlog.error(_("Failed to update profile with password: %s"), e.what());
      }
    }
  }
```

- [ ] **Step 6: Add include for parameters.h in CConn.cxx**

At the top of `CConn.cxx`, ensure this include is present (it likely already is at line 67):

```cpp
#include "parameters.h"
```

- [ ] **Step 7: Build to verify compilation**

Run:
```bash
cd build && cmake --build . --target vncviewer 2>&1 | head -30
```

Expected: Compiles successfully (ServerDialog changes come next, but it doesn't reference the removed statics).

- [ ] **Step 8: Commit**

```bash
git add vncviewer/CConn.h vncviewer/CConn.cxx
git commit -m "feat: replace in-memory saved credentials with profile-based password persistence"
```

---

### Task 5: Rebuild ServerDialog — Layout and Profile List

**Files:**
- Modify: `vncviewer/ServerDialog.h`
- Modify: `vncviewer/ServerDialog.cxx`

- [ ] **Step 1: Rewrite ServerDialog.h**

Replace the entire contents of `vncviewer/ServerDialog.h`:

```cpp
/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2026 TigerVNC Contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifndef __SERVERDIALOG_H__
#define __SERVERDIALOG_H__

#include <FL/Fl_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <string>
#include <vector>

#include "parameters.h"

class Fl_Widget;

class ServerDialog : public Fl_Window {
protected:
  ServerDialog();
  ~ServerDialog();

public:
  static void run(const char* servername, char *newservername);

protected:
  static void handleOptions(Fl_Widget *widget, void *data);
  static void handleConnect(Fl_Widget *widget, void *data);
  static void handleAbout(Fl_Widget *widget, void *data);
  static void handleCancel(Fl_Widget *widget, void *data);
  static void handleEdit(Fl_Widget *widget, void *data);
  static void handleDelete(Fl_Widget *widget, void *data);
  static void handleProfileSelect(Fl_Widget *widget, void *data);
  static void handleHostInput(Fl_Widget *widget, void *data);

private:
  void loadProfiles();
  void populateProfileList();
  void connectToProfile(int index);
  void connectToServer(const char* servername);
  int findProfileByServer(const char* servername);

protected:
  Fl_Input *serverInput;
  Fl_Hold_Browser *profileList;
  std::vector<ProfileInfo> profiles;
};

#endif
```

- [ ] **Step 2: Rewrite ServerDialog.cxx — includes and constructor**

Replace the entire contents of `vncviewer/ServerDialog.cxx`. Start with includes and constructor:

```cpp
/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2026 TigerVNC Contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <algorithm>

#ifdef WIN32
#include <winsock2.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>

#include <core/LogWriter.h>
#include <network/TcpSocket.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "CConn.h"
#include "i18n.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

ServerDialog::ServerDialog()
  : Fl_Window(500, 0, "TigerVNC")
{
  int x, y, x2;
  Fl_Button *button;
  Fl_Box *divider;

  x = OUTER_MARGIN;
  y = OUTER_MARGIN;

  // Host input + Connect button at top
  int connectBtnW = BUTTON_WIDTH;
  serverInput = new Fl_Input(
    LBLLEFT(x, y, w() - OUTER_MARGIN*2 - connectBtnW - INNER_MARGIN,
            INPUT_HEIGHT, _("VNC server:"))
  );
  serverInput->when(FL_WHEN_ENTER_KEY);
  serverInput->callback(handleConnect, this);

  button = new Fl_Return_Button(
    w() - OUTER_MARGIN - connectBtnW, y,
    connectBtnW, BUTTON_HEIGHT, _("Connect"));
  button->callback(handleConnect, this);

  y += INPUT_HEIGHT + INNER_MARGIN;

  // Profile list
  int listH = 180;
  profileList = new Fl_Hold_Browser(x, y, w() - OUTER_MARGIN*2, listH);
  profileList->column_widths(new int[3]{30, 200, 0});
  profileList->column_char('\t');
  profileList->callback(handleProfileSelect, this);
  profileList->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);

  y += listH + INNER_MARGIN;

  // Button bar
  x2 = x;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Options..."));
  button->callback(handleOptions, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Edit..."));
  button->callback(handleEdit, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Delete"));
  button->callback(handleDelete, this);

  // About button on the right
  button = new Fl_Button(w() - OUTER_MARGIN - BUTTON_WIDTH, y,
                         BUTTON_WIDTH, BUTTON_HEIGHT, _("About..."));
  button->callback(handleAbout, this);

  y += BUTTON_HEIGHT + INNER_MARGIN;

  divider = new Fl_Box(0, y, w(), 2);
  divider->box(FL_THIN_DOWN_FRAME);
  y += divider->h() + INNER_MARGIN;

  // Cancel button at bottom right
  y += OUTER_MARGIN - INNER_MARGIN;
  button = new Fl_Button(w() - OUTER_MARGIN - BUTTON_WIDTH, y,
                         BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  button->callback(handleCancel, this);

  y += BUTTON_HEIGHT + INNER_MARGIN;

  resizable(nullptr);
  h(y - INNER_MARGIN + OUTER_MARGIN);

  callback(handleCancel, this);
}

ServerDialog::~ServerDialog()
{
}
```

- [ ] **Step 3: Implement ServerDialog::run() and profile loading**

Continue in `ServerDialog.cxx`:

```cpp
void ServerDialog::run(const char* servername, char *newservername)
{
  ServerDialog dialog;

  dialog.serverInput->value(servername);

  dialog.show();

  try {
    dialog.loadProfiles();
    dialog.populateProfileList();
  } catch (std::exception& e) {
    vlog.error(_("Unable to load profiles: %s"), e.what());
  }

  while (dialog.shown()) Fl::wait();

  if (dialog.serverInput->value() == nullptr ||
      dialog.serverInput->value()[0] == '\0') {
    newservername[0] = '\0';
    return;
  }

  strncpy(newservername, dialog.serverInput->value(), VNCSERVERNAMELEN);
  newservername[VNCSERVERNAMELEN - 1] = '\0';
}

void ServerDialog::loadProfiles()
{
  profiles = loadAllProfiles();
}

void ServerDialog::populateProfileList()
{
  profileList->clear();

  for (const ProfileInfo& p : profiles) {
    std::string displayName = p.profileName.empty() ?
                              p.serverName : p.profileName;
    std::string lockIcon = p.hasPassword ? "@lock" : "";
    std::string entry = lockIcon + "\t" + displayName + "\t" +
                        p.serverName;
    profileList->add(entry.c_str());
  }
}
```

Note: FLTK `@lock` is not a real symbol — we'll use a simple text indicator instead. Replace `"@lock"` with `"\xe2\x80\xa2"` (bullet) for has-password and `" "` for no-password. Actually, FLTK doesn't support unicode well in browsers. Use simple ASCII:

```cpp
    std::string indicator = p.hasPassword ? "[*]" : "[ ]";
    std::string entry = indicator + "\t" + displayName + "\t" +
                        p.serverName;
```

- [ ] **Step 4: Implement connect handlers**

Continue in `ServerDialog.cxx`:

```cpp
int ServerDialog::findProfileByServer(const char* servername)
{
  std::string host;
  int port;

  try {
    network::getHostAndPort(servername, &host, &port);
  } catch (std::exception&) {
    return -1;
  }

  for (size_t i = 0; i < profiles.size(); i++) {
    std::string pHost;
    int pPort;
    try {
      network::getHostAndPort(profiles[i].serverName.c_str(),
                              &pHost, &pPort);
    } catch (std::exception&) {
      continue;
    }
    if (pHost == host && pPort == port)
      return (int)i;
  }
  return -1;
}

void ServerDialog::connectToProfile(int index)
{
  if (index < 0 || index >= (int)profiles.size())
    return;

  const ProfileInfo& profile = profiles[index];

  // Load full parameters from the profile file
  try {
    loadViewerParameters(profile.filePath.c_str());
  } catch (std::exception& e) {
    vlog.error(_("Failed to load profile settings: %s"), e.what());
  }

  // Set the active profile path for CConn to use
  CConn::activeProfilePath = profile.filePath;

  serverInput->value(profile.serverName.c_str());
  hide();
}

void ServerDialog::connectToServer(const char* servername)
{
  // Check if there's a matching profile
  int idx = findProfileByServer(servername);
  if (idx >= 0) {
    connectToProfile(idx);
    return;
  }

  // New server — auto-create a profile (without password for now)
  try {
    saveProfile(servername, nullptr, nullptr);
    // Set active profile path
    std::string host;
    int port;
    network::getHostAndPort(servername, &host, &port);
    std::string dir = getProfilesDir();
    std::string filename = getProfileFilename(host.c_str(), port);
    CConn::activeProfilePath = dir + "/" + filename;
  } catch (std::exception& e) {
    vlog.error(_("Failed to save profile: %s"), e.what());
  }

  hide();
}
```

- [ ] **Step 5: Implement callback handlers**

Continue in `ServerDialog.cxx`:

```cpp
void ServerDialog::handleConnect(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverInput->value();

  if (servername == nullptr || servername[0] == '\0')
    return;

  try {
    saveViewerParameters(nullptr, servername);
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the default configuration: %s"),
               e.what());
  }

  dialog->connectToServer(servername);
}

void ServerDialog::handleProfileSelect(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int selected = dialog->profileList->value();

  if (selected <= 0)
    return;

  // Double-click = immediate connect
  if (Fl::event_clicks()) {
    dialog->connectToProfile(selected - 1);
    return;
  }

  // Single click = populate host field
  if (selected - 1 < (int)dialog->profiles.size()) {
    dialog->serverInput->value(
      dialog->profiles[selected - 1].serverName.c_str());
  }
}

void ServerDialog::handleOptions(Fl_Widget* /*widget*/, void* /*data*/)
{
  OptionsDialog::showDialog();
}

void ServerDialog::handleAbout(Fl_Widget* /*widget*/, void* /*data*/)
{
  about_vncviewer();
}

void ServerDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->serverInput->value("");
  dialog->hide();
}

void ServerDialog::handleEdit(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int selected = dialog->profileList->value();

  if (selected <= 0 || selected - 1 >= (int)dialog->profiles.size()) {
    fl_alert(_("Please select a profile to edit."));
    return;
  }

  ProfileInfo& profile = dialog->profiles[selected - 1];

  const char* newName = fl_input(_("Profile nickname:"),
                                 profile.profileName.empty() ?
                                 profile.serverName.c_str() :
                                 profile.profileName.c_str());

  if (newName == nullptr)
    return;

  try {
    // Re-save profile with new name, preserving password
    std::string existingPwd = loadPasswordFromProfile(profile.filePath.c_str());
    saveProfileToFile(profile.filePath.c_str(),
                      profile.serverName.c_str(),
                      newName,
                      existingPwd.empty() ? nullptr : existingPwd.c_str());
    profile.profileName = newName;
    dialog->populateProfileList();
  } catch (std::exception& e) {
    vlog.error(_("Failed to update profile: %s"), e.what());
    fl_alert(_("Failed to update profile:\n\n%s"), e.what());
  }
}

void ServerDialog::handleDelete(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int selected = dialog->profileList->value();

  if (selected <= 0 || selected - 1 >= (int)dialog->profiles.size()) {
    fl_alert(_("Please select a profile to delete."));
    return;
  }

  const ProfileInfo& profile = dialog->profiles[selected - 1];
  std::string displayName = profile.profileName.empty() ?
                            profile.serverName : profile.profileName;

  int choice = fl_choice(_("Delete profile \"%s\"?"),
                         _("Cancel"), _("Delete"), nullptr,
                         displayName.c_str());
  if (choice != 1)
    return;

  try {
    deleteProfile(profile.filePath.c_str());
    dialog->profiles.erase(dialog->profiles.begin() + (selected - 1));
    dialog->populateProfileList();
  } catch (std::exception& e) {
    vlog.error(_("Failed to delete profile: %s"), e.what());
    fl_alert(_("Failed to delete profile:\n\n%s"), e.what());
  }
}

void ServerDialog::handleHostInput(Fl_Widget* /*widget*/, void* data)
{
  // Reserved for future host-field filtering
  (void)data;
}
```

- [ ] **Step 6: Build to verify compilation**

Run:
```bash
cd build && cmake --build . --target vncviewer 2>&1 | tail -5
```
Expected: Compiles successfully.

- [ ] **Step 7: Commit**

```bash
git add vncviewer/ServerDialog.h vncviewer/ServerDialog.cxx
git commit -m "feat: rebuild ServerDialog with profile list and one-click connect"
```

---

### Task 6: Update vncviewer.cxx — Startup and Migration

**Files:**
- Modify: `vncviewer/vncviewer.cxx`

- [ ] **Step 1: Add profiles directory creation on startup**

In `vncviewer/vncviewer.cxx`, find the block around line 662 that loads default parameters. Add profile directory creation right after:

```cpp
  /* Load the default parameter settings */
  char defaultServerName[VNCSERVERNAMELEN] = "";
  try {
    const char* configServerName;
    configServerName = loadViewerParameters(nullptr);
    if (configServerName != nullptr) {
      strncpy(defaultServerName, configServerName, VNCSERVERNAMELEN-1);
      defaultServerName[VNCSERVERNAMELEN-1] = '\0';
    }
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
  }

  /* Ensure profiles directory exists */
  try {
    std::string profilesDir = getProfilesDir();
#ifdef _WIN32
    wchar_t wdir[PATH_MAX];
    fl_utf8towc(profilesDir.c_str(), profilesDir.size()+1, wdir, PATH_MAX);
    CreateDirectoryW(wdir, nullptr);
#else
    mkdir(profilesDir.c_str(), 0755);
#endif
  } catch (std::exception& e) {
    vlog.error(_("Could not create profiles directory: %s"), e.what());
  }
```

- [ ] **Step 2: Add one-time migration from old history**

Add after the profiles directory creation:

```cpp
  /* Migrate old server history to profiles (one-time) */
  try {
    std::string profilesDir = getProfilesDir();
    std::vector<ProfileInfo> existing = loadAllProfiles();

    if (existing.empty()) {
      // No profiles yet — migrate from old history
#ifdef _WIN32
      std::list<std::string> history = loadHistoryFromRegKey();
#else
      std::list<std::string> history;
      const char* stateDir = core::getvncstatedir();
      if (stateDir != nullptr) {
        char histpath[PATH_MAX];
        snprintf(histpath, sizeof(histpath), "%s/tigervnc.history", stateDir);
        FILE* hf = fopen(histpath, "r");
        if (hf) {
          char hline[256];
          while (fgets(hline, sizeof(hline), hf)) {
            int hlen = strlen(hline);
            if (hlen > 0 && hline[hlen-1] == '\n') hline[hlen-1] = '\0';
            if (hline[0] != '\0')
              history.push_back(hline);
          }
          fclose(hf);
        }
      }
#endif

      for (const std::string& server : history) {
        try {
          saveProfile(server.c_str(), nullptr, nullptr);
        } catch (std::exception& e) {
          vlog.error(_("Failed to migrate server \"%s\": %s"),
                     server.c_str(), e.what());
        }
      }

      // Also migrate default config server if not in history
      if (defaultServerName[0] != '\0') {
        bool found = false;
        for (const std::string& s : history) {
          if (s == defaultServerName) { found = true; break; }
        }
        if (!found) {
          try {
            saveProfile(defaultServerName, nullptr, nullptr);
          } catch (std::exception&) {}
        }
      }
    }
  } catch (std::exception& e) {
    vlog.error(_("Failed to migrate server history: %s"), e.what());
  }
```

- [ ] **Step 3: Remove old Fl_Suggestion_Input include if present**

In `vncviewer/ServerDialog.cxx`, the old include `#include "fltk/Fl_Suggestion_Input.h"` was already removed in Task 5 (full rewrite). Verify it's not referenced anywhere else in vncviewer that would break.

Run:
```bash
grep -r "Fl_Suggestion_Input" vncviewer/ --include="*.cxx" --include="*.h" | grep -v "fltk/"
```

If only the widget's own files reference it, we're clean.

- [ ] **Step 4: Build full project**

Run:
```bash
cd build && cmake .. && cmake --build . 2>&1 | tail -10
```
Expected: Full build succeeds.

- [ ] **Step 5: Commit**

```bash
git add vncviewer/vncviewer.cxx
git commit -m "feat: add profiles directory creation and history migration on startup"
```

---

### Task 7: Integration Verification and Cleanup

**Files:**
- Modify: `vncviewer/parameters.h` (cleanup if needed)
- Modify: `vncviewer/parameters.cxx` (cleanup if needed)

- [ ] **Step 1: Run all unit tests**

```bash
cd build && cmake --build . && ctest -V
```
Expected: All tests pass including the new profile tests.

- [ ] **Step 2: Verify the old SERVER_HISTORY_SIZE constant can be removed**

Check if `SERVER_HISTORY_SIZE` is still referenced:

```bash
grep -r "SERVER_HISTORY_SIZE" vncviewer/ --include="*.cxx" --include="*.h"
```

If only referenced in `parameters.h` (definition) and `parameters.cxx` (old `saveServerHistory` which was in `ServerDialog.cxx` — now removed), remove the `#define SERVER_HISTORY_SIZE 20` from `parameters.h`.

- [ ] **Step 3: Verify old history functions can be removed from parameters.h**

The declarations `loadHistoryFromRegKey()` and `saveHistoryToRegKey()` in `parameters.h` (lines 90-91) are still used during migration in `vncviewer.cxx`. Keep `loadHistoryFromRegKey()` but remove `saveHistoryToRegKey()` since we never write to the old history format.

In `parameters.h`, change:

```cpp
#ifdef _WIN32
std::list<std::string> loadHistoryFromRegKey();
void saveHistoryToRegKey(const std::list<std::string>& serverHistory);
#endif
```

To:

```cpp
#ifdef _WIN32
std::list<std::string> loadHistoryFromRegKey();
#endif
```

In `parameters.cxx`, keep the `loadHistoryFromRegKey()` implementation but remove `saveHistoryToRegKey()` (lines 504-535).

- [ ] **Step 4: Remove SERVER_HISTORY constant from old ServerDialog**

Since ServerDialog.cxx was fully rewritten in Task 5, verify the old `SERVER_HISTORY` constant and `SERVER_HISTORY_SIZE` references are gone:

```bash
grep -r "SERVER_HISTORY" vncviewer/ --include="*.cxx" --include="*.h"
```

Remove any remaining references in `parameters.h` if present.

- [ ] **Step 5: Full rebuild and test**

```bash
cd build && cmake .. && cmake --build . && ctest -V
```
Expected: Clean build, all tests pass.

- [ ] **Step 6: Commit**

```bash
git add vncviewer/parameters.h vncviewer/parameters.cxx
git commit -m "chore: remove unused server history write functions and constants"
```

---

## Summary

| Task | What it delivers |
|---|---|
| 1 | Profile filename generation, password hex encode/decode, data structures, unit tests |
| 2 | Full profile save/load/scan/delete I/O layer with unit tests |
| 3 | AuthDialog "Save password" checkbox (always visible when password needed) |
| 4 | CConn profile-based password persistence (replaces in-memory statics) |
| 5 | Rebuilt ServerDialog with profile list, double-click connect, edit/delete |
| 6 | Startup profiles directory creation and old history migration |
| 7 | Integration verification, cleanup of dead code |
