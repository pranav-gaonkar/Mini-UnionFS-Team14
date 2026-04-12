#include "common.h"

#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>

static int ensure_parent_dirs(const fs::path &path) {
    auto parent = path.parent_path();
    if (parent.empty()) {
        return 0;
    }
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (ec) {
        return -ec.value();
    }
    return 0;
}

static int write_whiteout(const char *path) {
    auto *state = get_state();
    auto rel = rel_from_fuse(path);
    if (rel.empty()) {
        return -EINVAL;
    }
    auto whiteout = make_whiteout_path(state, rel);
    if (whiteout.empty()) {
        return -EINVAL;
    }
    int rc = ensure_parent_dirs(whiteout);
    if (rc != 0) {
        return rc;
    }
    int fd = ::open(whiteout.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        return -errno;
    }
    ::close(fd);
    return 0;
}

static void remove_whiteout_entry(const fs::path &rel) {
    if (rel.empty()) {
        return;
    }
    auto *state = get_state();
    auto whiteout = make_whiteout_path(state, rel);
    if (whiteout.empty()) {
        return;
    }
    if (::unlink(whiteout.c_str()) == -1 && errno != ENOENT) {
        // Best-effort cleanup; ignore failures.
    }
}

int unionfs_unlink(const char *path) {
    auto *state = get_state();
    auto rel = rel_from_fuse(path);
    auto upper = make_branch_path(state->upper_dir, rel);
    auto lower = make_branch_path(state->lower_dir, rel);
    bool lower_exists = path_exists(lower);

    if (path_exists(upper)) {
        if (::unlink(upper.c_str()) == -1) {
            return -errno;
        }
        if (lower_exists) {
            return write_whiteout(path);
        }
        remove_whiteout_entry(rel);
        return 0;
    }

    if (lower_exists) {
        return write_whiteout(path);
    }

    return -ENOENT;
}

int unionfs_rmdir(const char *path) {
    auto *state = get_state();
    auto rel = rel_from_fuse(path);
    auto upper = make_branch_path(state->upper_dir, rel);
    auto lower = make_branch_path(state->lower_dir, rel);
    bool lower_exists = path_exists(lower);

    if (::rmdir(upper.c_str()) == 0) {
        if (lower_exists) {
            return write_whiteout(path);
        }
        remove_whiteout_entry(rel);
        return 0;
    }
    if (errno != ENOENT) {
        return -errno;
    }

    if (lower_exists) {
        return write_whiteout(path);
    }
    return -ENOENT;
}
