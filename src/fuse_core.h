#ifndef UNIONFS_FUSE_CORE_H
#define UNIONFS_FUSE_CORE_H

struct fuse_operations;
struct fuse_conn_info;
struct fuse_config;

// FUSE initialization callback
void *unionfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg);

// Get the complete FUSE operations structure
struct fuse_operations make_fuse_operations();

// Main entry point
int main(int argc, char *argv[]);

#endif // UNIONFS_FUSE_CORE_H
