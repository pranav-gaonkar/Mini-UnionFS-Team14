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
git clone https://github.com/pranav-gaonkar/Mini-UnionFS-Team14.git
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

## Demo Workflow (Copy-Paste Commands)
Use this exact sequence for a clean classroom demo on Windows + WSL2.

### Terminal Setup
- Open two terminals:
	- **Terminal A**: run and keep the FUSE process in foreground.
	- **Terminal B**: run verification/demo commands.
- Start from PowerShell in project root (`D:\Mini-UnionFS-Team`).

### 1) Build
Run in PowerShell:
```powershell
cd D:\Mini-UnionFS-Team
wsl -d Ubuntu make -C /mnt/d/Mini-UnionFS-Team
```

### 2) Create a Fresh Demo Environment (native Linux path)
Run in PowerShell:
```powershell
wsl -d Ubuntu bash -lc 'rm -rf ~/unionfs_demo && mkdir -p ~/unionfs_demo/lower ~/unionfs_demo/upper ~/unionfs_demo/mnt && cp /etc/hosts ~/unionfs_demo/lower/base.txt'
```

### 3) Start UnionFS (Terminal A)
Run in **Terminal A** and keep it running:
```powershell
wsl -d Ubuntu -e bash -lc 'cd ~/unionfs_demo && /mnt/d/Mini-UnionFS-Team/mini_unionfs lower upper mnt -f -o auto_unmount'
```

### 4) Verify Mount (Terminal B)
Run in **Terminal B**:
```powershell
wsl -d Ubuntu findmnt ~/unionfs_demo/mnt
wsl -d Ubuntu ls -la ~/unionfs_demo/mnt
```

### 5) Show Upper Layer Starts Empty
Run in **Terminal B**:
```powershell
wsl -d Ubuntu ls -la ~/unionfs_demo/upper
```

### 6) Trigger Copy-on-Write
Modify file via mountpoint:
```powershell
wsl -d Ubuntu bash -lc 'echo "demo-write-1" >> ~/unionfs_demo/mnt/base.txt'
```
Now show promoted file in upper layer:
```powershell
wsl -d Ubuntu ls -la ~/unionfs_demo/upper
wsl -d Ubuntu bash -lc 'echo "LOWER tail:"; tail -n 1 ~/unionfs_demo/lower/base.txt; echo "UPPER tail:"; tail -n 2 ~/unionfs_demo/upper/base.txt'
```

### 7) Show New File Creation Goes to Upper
```powershell
wsl -d Ubuntu bash -lc 'echo "created via mount" > ~/unionfs_demo/mnt/new.txt && ls -la ~/unionfs_demo/upper'
```

### 8) Show Whiteout on Delete
Delete a lower-origin file through mountpoint:
```powershell
wsl -d Ubuntu bash -lc 'rm ~/unionfs_demo/mnt/base.txt && ls -la ~/unionfs_demo/upper && echo "Merged view:" && ls -la ~/unionfs_demo/mnt'
```
Expected in upper layer: `.wh.base.txt`.

### 9) Optional Nautilus UI Steps (visual demo)
Open mount and upper folders visually:
```powershell
wsl -d Ubuntu nautilus ~/unionfs_demo/mnt &
wsl -d Ubuntu nautilus ~/unionfs_demo/upper &
```
If Nautilus interaction is flaky on WSLg, keep using Terminal B for edits and use Nautilus only as a viewer.

### 10) Stop and Clean Up
In **Terminal A**, press `Ctrl+C` to unmount.
Then run in **Terminal B**:
```powershell
wsl -d Ubuntu bash -lc 'findmnt ~/unionfs_demo/mnt || echo "Unmounted successfully"'
```
Optional cleanup:
```powershell
wsl -d Ubuntu rm -rf ~/unionfs_demo
```

### One-Line Demo Summary (for presentation)
"This demo shows UnionFS merged namespace behavior, copy-on-write promotion from lower to upper, and whiteout-based deletes (`.wh.<name>`), which are core layered filesystem semantics."
