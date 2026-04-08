# Mini-UnionFS

A teaching implementation of a minimal Union File System using FUSE 3. It overlays a writable upper directory on top of a read-only base layer, supporting copy-on-write semantics and whiteouts in the same spirit as container image layering.

## Features
- Layered namespace that prefers upper-layer entries over lower-layer ones.
- Copy-on-Write promotion when modifying files that originate in the lower layer.
- Whiteout files (`.wh.<name>`) to hide deleted lower-layer entries.
- Core POSIX operations: `getattr`, `readdir`, `read`, `write`, `create`, `unlink`, `mkdir`, `rmdir`, plus helpers (`open`, `release`, `truncate`, `utimens`, `access`) required for practical workloads.

## Team Responsibilities
- **Pranav G**: FUSE Core and System Integration
- **Prajwal Kittali**: Read and Metadata Operations
- **Prashant Radder**: Write Operations and Copy-on-Write Layer
- **Pranav Sharma**: Deletion Handling and Testing

## Requirements
- Linux (or WSL2) with FUSE 3 support.
- Packages: `build-essential`, `pkg-config`, `fuse3`, `libfuse3-dev`.
- Run on a native Linux filesystem (e.g., ext4 inside WSL2) when mounting; FUSE cannot mount over Windows `drvfs` paths such as `/mnt/c` or `/mnt/d`.

## Build
```bash
sudo apt update && sudo apt install -y build-essential pkg-config fuse3 libfuse3-dev
git clone https://github.com/pranav-gaonkar/Mini-UnionFS.git
cd Mini-UnionFS
make
```
Artifacts:
- `mini_unionfs` binary in the project root.
- Intermediate objects under `build/`.

Use `make clean` to remove generated files.

## Usage
```bash
./mini_unionfs <lower_dir> <upper_dir> <mountpoint> [-f] [-o opt1,opt2,...]
```
Example session (all paths on a native Linux filesystem):
```bash
mkdir -p lower upper mnt
cp /etc/hosts lower/base.txt   # sample base content
./mini_unionfs lower upper mnt -f -o auto_unmount
# in another shell
ls mnt
echo "hello" >> mnt/base.txt   # triggers Copy-on-Write into upper/base.txt
rm mnt/base.txt                # creates upper/.wh.base.txt hiding the lower copy
```
While mounted, any writes go to the upper directory, preserving the lower directory untouched. Whiteouts are created in the upper directory to hide deletions originating from the lower layer.

## Testing
The smoke suite in `scripts/test_unionfs.sh` reproduces the scenarios from the assignment appendix (visibility, CoW, whiteout). Run from the repo root:
```bash
bash scripts/test_unionfs.sh
```
The script builds a temporary environment, mounts the filesystem in foreground mode, exercises the three behaviors, and unmounts/cleans up automatically. If you see `mounting over filesystem type 0x01021997 is forbidden`, move the project to a native Linux path before rerunning.
