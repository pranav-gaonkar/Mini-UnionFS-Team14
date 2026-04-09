#include "common.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>
#include <vector>

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

static int copy_up_symlink(const fs::path &lower, const fs::path &upper, const struct stat &st) {
    size_t buf_size = static_cast<size_t>(std::max<off_t>(st.st_size + 1, static_cast<off_t>(PATH_MAX)));
    std::vector<char> target(buf_size, '\0');
    ssize_t len = ::readlink(lower.c_str(), target.data(), target.size() - 1);
    if (len < 0) {
        return -errno;
    }
    target[static_cast<size_t>(len)] = '\0';
    if (::symlink(target.data(), upper.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int copy_up_regular_file(const fs::path &lower, const fs::path &upper, const struct stat &st) {
    int in_fd = ::open(lower.c_str(), O_RDONLY);
    if (in_fd == -1) {
        return -errno;
    }
    int out_fd = ::open(upper.c_str(), O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (out_fd == -1) {
        ::close(in_fd);
        return -errno;
    }

    constexpr size_t BUF_SIZE = 1 << 16;
    std::vector<char> buffer(BUF_SIZE);
    ssize_t read_bytes;
    while ((read_bytes = ::read(in_fd, buffer.data(), buffer.size())) > 0) {
        ssize_t written = 0;
        while (written < read_bytes) {
            ssize_t w = ::write(out_fd, buffer.data() + written, static_cast<size_t>(read_bytes - written));
            if (w == -1) {
                int err = errno;
                ::close(in_fd);
                ::close(out_fd);
                return -err;
            }
            written += w;
        }
    }

    if (read_bytes == -1) {
        int err = errno;
        ::close(in_fd);
        ::close(out_fd);
        return -err;
    }

    ::close(in_fd);
    ::close(out_fd);
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

static int copy_up_file(const fs::path &lower, const fs::path &upper) {
    struct stat st;
    if (::lstat(lower.c_str(), &st) == -1) {
        return -errno;
    }

    int rc = ensure_parent_dirs(upper);
    if (rc != 0) {
        return rc;
    }

    if (S_ISLNK(st.st_mode)) {
        return copy_up_symlink(lower, upper, st);
    }

    if (S_ISREG(st.st_mode)) {
        rc = copy_up_regular_file(lower, upper, st);
        if (rc != 0) {
            return rc;
        }
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        if (::mkdir(upper.c_str(), st.st_mode & 0777) == -1 && errno != EEXIST) {
            return -errno;
        }
        return 0;
    }

    return -EINVAL;
}

bool needs_write_access(int flags) {
    return (flags & O_ACCMODE) != O_RDONLY;
}

int ensure_cow(const char *path, bool for_write) {
    if (!for_write) {
        return 0;
    }
    auto *state = get_state();
    auto rel = rel_from_fuse(path);
    auto upper = make_branch_path(state->upper_dir, rel);
    if (path_exists(upper)) {
        return 0;
    }
    auto lower = make_branch_path(state->lower_dir, rel);
    if (!path_exists(lower)) {
        return -ENOENT;
    }
    int rc = copy_up_file(lower, upper);
    if (rc == 0) {
        remove_whiteout_entry(rel);
    }
    return rc;
}

int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    int fd = static_cast<int>(fi->fh);
    ssize_t res = ::pwrite(fd, buf, size, offset);
    if (res == -1) {
        return -errno;
    }
    return static_cast<int>(res);
}

int unionfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void)fi;
    int rc = ensure_cow(path, true);
    if (rc != 0) {
        return rc;
    }
    auto resolved = resolve_path(path);
    if (resolved.origin == EntryOrigin::Whiteout || resolved.origin == EntryOrigin::None) {
        return -ENOENT;
    }
    if (::truncate(resolved.path.c_str(), size) == -1) {
        return -errno;
    }
    return 0;
}

int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    auto *state = get_state();
    auto rel = rel_from_fuse(path);
    auto upper = make_branch_path(state->upper_dir, rel);

    int rc = ensure_parent_dirs(upper);
    if (rc != 0) {
        return rc;
    }

    int fd = ::open(upper.c_str(), fi->flags | O_CREAT, mode);
    if (fd == -1) {
        return -errno;
    }
    remove_whiteout_entry(rel);
    fi->fh = static_cast<uint64_t>(fd);
    return 0;
}

int unionfs_mkdir(const char *path, mode_t mode) {
    auto *state = get_state();
    auto rel = rel_from_fuse(path);
    auto upper = make_branch_path(state->upper_dir, rel);
    int rc = ensure_parent_dirs(upper);
    if (rc != 0) {
        return rc;
    }
    if (::mkdir(upper.c_str(), mode) == -1) {
        return -errno;
    }
    remove_whiteout_entry(rel);
    return 0;
}

int unionfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi) {
    (void)fi;
    int rc = ensure_cow(path, true);
    if (rc != 0) {
        return rc;
    }
    auto resolved = resolve_path(path);
    if (resolved.origin == EntryOrigin::Whiteout || resolved.origin == EntryOrigin::None) {
        return -ENOENT;
    }
    if (::utimensat(AT_FDCWD, resolved.path.c_str(), ts, 0) == -1) {
        return -errno;
    }
    return 0;
}
