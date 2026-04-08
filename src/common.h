#ifndef UNIONFS_COMMON_H
#define UNIONFS_COMMON_H

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// ============================================================================
// Data Structures
// ============================================================================

struct MiniUnionFSState {
    fs::path lower_dir;
    fs::path upper_dir;
};

enum class EntryOrigin {
    None,
    Upper,
    Lower,
    Whiteout
};

struct ResolvedPath {
    EntryOrigin origin = EntryOrigin::None;
    fs::path path;
};

// ============================================================================
// Shared Utility Functions (Path Resolution)
// ============================================================================

// Get the FUSE context state from the current request
static MiniUnionFSState *get_state() {
    return static_cast<MiniUnionFSState *>(fuse_get_context()->private_data);
}

// Convert a FUSE path to a relative path
static fs::path rel_from_fuse(const char *path) {
    if (path == nullptr || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return fs::path();
    }
    std::string rel(path);
    if (!rel.empty() && rel.front() == '/') {
        rel.erase(0, 1);
    }
    return fs::path(rel);
}

// Construct the full path for a file in a branch
static fs::path make_branch_path(const fs::path &base, const fs::path &rel) {
    if (rel.empty()) {
        return base;
    }
    return base / rel;
}

// Construct the whiteout filename path in the upper layer
static fs::path make_whiteout_path(const MiniUnionFSState *state, const fs::path &rel) {
    if (rel.empty()) {
        return fs::path();
    }
    auto parent = rel.parent_path();
    auto filename = rel.filename().string();
    if (filename.empty()) {
        return fs::path();
    }
    fs::path result = state->upper_dir;
    if (!parent.empty()) {
        result /= parent;
    }
    result /= ".wh." + filename;
    return result;
}

// Check if a path exists using lstat (respects symlinks)
static bool lstat_path(const fs::path &path, struct stat *st = nullptr) {
    struct stat tmp;
    if (st == nullptr) {
        st = &tmp;
    }
    return ::lstat(path.c_str(), st) == 0;
}

// Check if a path exists
static bool path_exists(const fs::path &path) {
    struct stat st;
    return lstat_path(path, &st);
}

// Resolve a FUSE path to its actual location (upper/lower/whiteout)
static ResolvedPath resolve_path(const char *fuse_path) {
    ResolvedPath resolved;
    auto *state = get_state();
    auto rel = rel_from_fuse(fuse_path);

    if (!rel.empty()) {
        auto whiteout = make_whiteout_path(state, rel);
        if (!whiteout.empty() && path_exists(whiteout)) {
            resolved.origin = EntryOrigin::Whiteout;
            return resolved;
        }
    }

    auto upper = make_branch_path(state->upper_dir, rel);
    if (path_exists(upper)) {
        resolved.origin = EntryOrigin::Upper;
        resolved.path = std::move(upper);
        return resolved;
    }

    auto lower = make_branch_path(state->lower_dir, rel);
    if (path_exists(lower)) {
        resolved.origin = EntryOrigin::Lower;
        resolved.path = std::move(lower);
        return resolved;
    }

    resolved.origin = EntryOrigin::None;
    return resolved;
}

// ============================================================================
// Forward Declarations (for modular implementation)
// ============================================================================

// Read/Metadata operations (prajwal/read_ops.cpp)
int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int unionfs_access(const char *path, int mask);
int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, 
                    struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int unionfs_open(const char *path, struct fuse_file_info *fi);
int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int unionfs_release(const char *path, struct fuse_file_info *fi);

// Write operations (prashant/write_ops.cpp)
int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int unionfs_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int unionfs_mkdir(const char *path, mode_t mode);
int unionfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi);

// Deletion operations (pranav_sharma/deletion_ops.cpp)
int unionfs_unlink(const char *path);
int unionfs_rmdir(const char *path);

#endif // UNIONFS_COMMON_H
