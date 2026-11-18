#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// A cut remains a set of locators on one selected drive until each move is
// confirmed complete. Clipboard contents alone never authorize a move.
class NativeWorkspaceCutState final {
public:
    void begin(std::string token, std::string drive, std::vector<std::string> paths) {
        token_=std::move(token); drive_=std::move(drive); paths_=std::move(paths);
    }
    [[nodiscard]] bool matches(const std::string &token, const std::string &drive,
                               const std::vector<std::string> &paths) const {
        return !token_.empty() && !paths_.empty() && token_==token && drive_==drive && paths_==paths;
    }
    void completed(const std::string &path) {
        std::erase(paths_,path);
        if (paths_.empty()) clear();
    }
    void clear() { token_.clear(); drive_.clear(); paths_.clear(); }
    [[nodiscard]] const std::string &token() const { return token_; }
    [[nodiscard]] const std::string &drive() const { return drive_; }
    [[nodiscard]] const std::vector<std::string> &paths() const { return paths_; }
private:
    std::string token_, drive_;
    std::vector<std::string> paths_;
};
