#pragma once
#include <string>
#include <vector>
#include <functional>

namespace materializr {

struct FileFilter {
    std::string description;
    std::string pattern;
};

class FileDialogs {
public:
    // Open a file browser (non-blocking, renders via ImGui)
    static void openFile(const std::string& title,
                         const std::vector<FileFilter>& filters,
                         std::function<void(const std::string&)> callback);

    // Open a save browser (non-blocking)
    static void saveFile(const std::string& title,
                         const std::string& defaultName,
                         const std::vector<FileFilter>& filters,
                         std::function<void(const std::string&)> callback);

    // Call every frame from the main loop to render the active dialog
    static void render();

    // Is a dialog currently open?
    static bool isOpen();

    // Last directory the picker landed in. Application syncs this with
    // AppSettings::lastFileDir at load / save time so the value survives
    // a relaunch. Updated automatically when a non-empty path comes back
    // from openFile / saveFile.
    static void setLastDir(const std::string& dir);
    static const std::string& getLastDir();
};

} // namespace materializr
