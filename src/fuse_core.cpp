#include "common.h"
#include <cstdlib>
#include <cstdio>
#include <fuse3/fuse.h>
#include <filesystem>
#include <vector>
#include <system_error>

// ============================================================================
// FUSE Core Operations (Pranav G)
// ============================================================================

// FUSE initialization: configure caching and timeouts
void *unionfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void)conn;
    cfg->use_ino = 1;
    cfg->entry_timeout = 0.1;
    cfg->attr_timeout = 0.1;
    cfg->negative_timeout = 0;
    return get_state();
}

// Build and return the complete FUSE operations structure
struct fuse_operations make_fuse_operations() {
    struct fuse_operations ops{};
    ops.init = unionfs_init;
    ops.getattr = unionfs_getattr;
    ops.access = unionfs_access;
    ops.readdir = unionfs_readdir;
    ops.open = unionfs_open;
    ops.read = unionfs_read;
    ops.write = unionfs_write;
    ops.release = unionfs_release;
    ops.truncate = unionfs_truncate;
    ops.create = unionfs_create;
    ops.unlink = unionfs_unlink;
    ops.mkdir = unionfs_mkdir;
    ops.rmdir = unionfs_rmdir;
    ops.utimens = unionfs_utimens;
    return ops;
}

static struct fuse_operations unionfs_oper = make_fuse_operations();

// Print usage information
static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mountpoint> [FUSE options...]\n", prog);
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    MiniUnionFSState state;
    std::error_code ec;
    state.lower_dir = std::filesystem::canonical(argv[1], ec);
    if (ec) {
        fprintf(stderr, "Failed to resolve lower_dir '%s': %s\n", argv[1], ec.message().c_str());
        return EXIT_FAILURE;
    }
    state.upper_dir = std::filesystem::canonical(argv[2], ec);
    if (ec) {
        fprintf(stderr, "Failed to resolve upper_dir '%s': %s\n", argv[2], ec.message().c_str());
        return EXIT_FAILURE;
    }

    int fuse_argc = argc - 2;
    std::vector<char *> fuse_argv;
    fuse_argv.reserve(static_cast<size_t>(fuse_argc));
    fuse_argv.push_back(argv[0]);
    fuse_argv.push_back(argv[3]);
    for (int i = 4; i < argc; ++i) {
        fuse_argv.push_back(argv[i]);
    }

    return fuse_main(fuse_argc, fuse_argv.data(), &unionfs_oper, &state);
}
