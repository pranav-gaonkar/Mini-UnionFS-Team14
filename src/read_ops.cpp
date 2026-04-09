#include "common.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unordered_set>
#include <unistd.h>

bool needs_write_access(int flags);
int ensure_cow(const char *path, bool for_write);

int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    auto resolved = resolve_path(path);
    if (resolved.origin == EntryOrigin::Whiteout || resolved.origin == EntryOrigin::None) {
        return -ENOENT;
    }

    if (::lstat(resolved.path.c_str(), stbuf) == -1) {
        return -errno;
    }
    return 0;
}

int unionfs_access(const char *path, int mask) {
    auto resolved = resolve_path(path);
    if (resolved.origin == EntryOrigin::Whiteout || resolved.origin == EntryOrigin::None) {
        return -ENOENT;
    }
    if (::access(resolved.path.c_str(), mask) == -1) {
        return -errno;
    }
    return 0;
}

int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    filler(buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    auto *state = get_state();
    auto rel = rel_from_fuse(path);

    std::unordered_set<std::string> emitted;
    std::unordered_set<std::string> whiteouts;

    auto scan_branch = [&](const fs::path &branch_root, bool is_upper) {
        auto absolute = make_branch_path(branch_root, rel);
        DIR *dir = ::opendir(absolute.c_str());
        if (!dir) {
            return;
        }
        struct dirent *entry;
        while ((entry = ::readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name == "." || name == "..") {
                continue;
            }
            if (is_upper && name.rfind(".wh.", 0) == 0) {
                whiteouts.insert(name.substr(4));
                continue;
            }
            if (whiteouts.count(name) > 0) {
                continue;
            }
            if (emitted.insert(name).second) {
                filler(buf, name.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
            }
        }
        ::closedir(dir);
    };

    scan_branch(state->upper_dir, true);
    scan_branch(state->lower_dir, false);

    return 0;
}

int unionfs_open(const char *path, struct fuse_file_info *fi) {
    bool write = needs_write_access(fi->flags);
    
    // If writing, ensure the file is copied to upper layer first
    if (write) {
        int rc = ensure_cow(path, true);
        if (rc != 0) {
            return rc;
        }
        
        // After copy-on-write, directly use the upper layer path
        auto *state = get_state();
        auto rel = rel_from_fuse(path);
        auto upper = make_branch_path(state->upper_dir, rel);
        
        // File should now be in upper after ensure_cow()
        if (!path_exists(upper)) {
            // If not in upper after ensure_cow, error
            return -EIO;
        }
        
        // Open the file from upper layer for writing
        int fd = ::open(upper.c_str(), fi->flags);
        if (fd == -1) {
            return -errno;
        }
        fi->fh = static_cast<uint64_t>(fd);
        return 0;
    }
    
    // For read-only access, resolve normally
    auto resolved = resolve_path(path);
    if (resolved.origin == EntryOrigin::Whiteout || resolved.origin == EntryOrigin::None) {
        return -ENOENT;
    }

    int fd = ::open(resolved.path.c_str(), fi->flags);
    if (fd == -1) {
        return -errno;
    }
    fi->fh = static_cast<uint64_t>(fd);
    return 0;
}

int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    int fd = static_cast<int>(fi->fh);
    ssize_t res = ::pread(fd, buf, size, offset);
    if (res == -1) {
        return -errno;
    }
    return static_cast<int>(res);
}

int unionfs_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    int fd = static_cast<int>(fi->fh);
    ::close(fd);
    return 0;
}
