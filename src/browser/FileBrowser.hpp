#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct DirEntry {
    std::filesystem::path path;
    std::string           display_name;
    bool                  is_directory = false;
    int64_t               file_size    = 0;
};

// Simple directory browser rooted at a given path.
// All operations are synchronous and UI-thread-only.
class FileBrowser {
public:
    explicit FileBrowser(std::filesystem::path root = std::filesystem::current_path());

    void setRoot(std::filesystem::path root);
    void navigate(const std::filesystem::path& dir);
    void navigateUp();
    void refreshCurrent();

    const std::filesystem::path&  currentPath() const { return current_; }
    const std::filesystem::path&  rootPath()    const { return root_; }
    const std::vector<DirEntry>&  entries()     const { return entries_; }

    bool atRoot() const { return current_ == root_; }
    bool atFilesystemRoot() const;

    // Returns true if the extension is a recognised audio format.
    static bool isAudioFile(const std::filesystem::path& p);
    static std::vector<std::filesystem::path> collectAudioFiles(const std::filesystem::path& dir,
                                                                bool recursive);

private:
    void refresh();

    std::filesystem::path root_;
    std::filesystem::path current_;
    std::vector<DirEntry> entries_;
};
