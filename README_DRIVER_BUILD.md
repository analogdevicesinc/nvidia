# ADI Driver Build Guide for NVIDIA L4T R36.5.0

This guide covers cloning the NVIDIA L4T R36.5.0 kernel sources, adding the
Analog Devices (ADI) remotes for `nvidia-oot` and `kernel-6-12-y`, and building
the kernel, out-of-tree modules, device tree blobs, and installing them.

> **Note:** This build uses `KERNEL_SRC_DIR=kernel-6-12-y` (Linux 6.12), not
> the default `kernel-jammy-src` (Ubuntu Jammy, Linux 5.15).

---

## Prerequisites

- x86_64 host running Ubuntu 20.04+ (or equivalent)
- `git`, `build-essential`, `bc`, `flex`, `bison`, `libssl-dev`, `libelf-dev`
- ~30 GB free disk space

```bash
sudo apt install git build-essential bc flex bison libssl-dev libelf-dev
```

---

## 1. Download the L4T Driver Package

Download the **L4T Driver Package (BSP) Sources R36.5.0** from the
[Driver Package (BSP)](https://developer.nvidia.com/downloads/embedded/l4t/r36_release_v5.0/release/Jetson_Linux_r36.5.0_aarch64.tbz2)
and extract it:

```bash
mkdir -p ~/nvidia && cd ~/nvidia
tar xf Jetson_Linux_r36.5.0_aarch64.tbz2
cd Linux_for_Tegra/source
```

This gives you the top-level build scripts (`nvbuild.sh`, `source_sync.sh`,
`kernel_src_build_env.sh`, `Makefile`).

IT is recommended to download the recommended cross compiler for r36.5.0 form [NVIDIA Jetson Linux Archive](https://developer.nvidia.com/embedded/jetson-linux-archive) and extract it in `Linux_for_Tegra/source/compiler`

---

## 2. Clone NVIDIA Sources

Use the provided `source_sync.sh` to clone all kernel and OOT module
repositories from NVIDIA's public GitLab. The `-k` flag downloads only kernel
and device tree repositories (skip userspace components):

```bash
./source_sync.sh -k -t jetson_36.5.0
```

> **Note:** `source_sync.sh` does **not** clone `kernel-6-12-y`. That kernel
> tree must be obtained separately (see next section).

---

## 3. Clone the kernel-6-12-y Tree

The `kernel-6-12-y` tree is not part of the default `source_sync.sh` manifest.
Clone it from the upstream stable kernel repository:

```bash
cd kernel/
git clone https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git kernel-6-12-y
cd kernel-6-12-y
git checkout v6.12
cd ../../
```

---

## 4. Add the ADI Remotes

Add the Analog Devices remotes for the `nvidia-oot` and `kernel-6-12-y`
repositories, then fetch the ADI branches:

### nvidia-oot

```bash
cd nvidia-oot
git remote add adi-nvidia-oot https://github.com/analogdevicesinc/nvidia.git
git fetch adi-nvidia-oot
git checkout gmsl/jetson_36.5.0-6.12/nvidia-oot
cd ..
```

### kernel-6-12-y

```bash
cd kernel/kernel-6-12-y
git remote add adi-kernel https://github.com/analogdevicesinc/linux.git
git fetch adi-kernel
git checkout gmsl/tegra-6.12.y
cd ../../
```

### Verify remotes

```bash
git -C nvidia-oot remote -v
# origin       https://gitlab.com/nvidia/nv-tegra/linux-nv-oot.git (fetch)
# adi-nvidia-oot   https://github.com/analogdevicesinc/nvidia.git (fetch)

git -C kernel/kernel-6-12-y remote -v
# origin       https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git (fetch)
# adi-kernel          https://github.com/analogdevicesinc/linux.git (fetch)
```

---

## 5. Update KERNEL_SRC_DIR

Edit `kernel_src_build_env.sh` directly:

```
KERNEL_SRC_DIR="kernel-6-12-y"
```

---

## 6. Set Up the Build Environment

### Cross-compilation from x86_64

```bash
export CROSS_COMPILE=$PWD/compiler/bin/aarch64-buildroot-linux-gnu-
```

### Set the install target (for later installation)

```bash
export INSTALL_MOD_PATH=/path/to/target/rootfs
```

---

## 7. Build Everything with nvbuild.sh

The simplest way to build kernel + OOT modules + DTBs in one shot:

```bash
./nvbuild.sh -o kernel_out
```

This will:
1. Rsync sources to `kernel_out/` (original tree is not modified)
2. Build the kernel Image, in-tree DTBs, and in-tree modules
3. Build all OOT modules (conftest -> hwpm -> nvidia-oot -> nvgpu -> nvdisplay)
4. Build NVIDIA DTBs

### nvbuild.sh options

| Flag | Description |
|------|-------------|
| (none) | Build kernel + OOT modules + DTBs |
| `-o <dir>` | Set output directory (default: `kernel_out/`) |
| `-r` | Enable PREEMPT_RT patches before building |
| `-m` | Build OOT modules only (skip kernel, requires `KERNEL_HEADERS`) |
| `-i` | Install kernel and modules to `INSTALL_MOD_PATH` |

---

## 8. Build Components Individually

### 8.1 Kernel only

```bash
make -C kernel/
```

This builds:
- Kernel Image (`kernel/kernel-6-12-y/arch/arm64/boot/Image`)
- In-tree DTBs
- In-tree modules

### 8.2 OOT modules only

Requires a pre-built kernel. Point `KERNEL_HEADERS` at the kernel build output:

```bash
export KERNEL_HEADERS=$PWD/kernel_out/kernel/kernel-6-12-y
make modules
```

Build order is enforced automatically:
1. **conftest** - kernel API compatibility headers
2. **hwpm** - Hardware Performance Monitor
3. **nvidia-oot** - main NVIDIA OOT modules (depends on hwpm symvers)
4. **nvgpu** - GPU driver (depends on nvidia-oot symvers)
5. **nvidia-display** - display/DRM driver (depends on merged nvidia-oot headers)

### 8.3 DTBs only

```bash
export KERNEL_HEADERS=$PWD/kernel_out/kernel/kernel-6-12-y
make dtbs
```

DTB output goes to `kernel-devicetree/generic-dts/dtbs/`.

---

## 9. Install

### Using nvbuild.sh

```bash
export INSTALL_MOD_PATH=/path/to/target/rootfs
./nvbuild.sh -i
```

This runs `sudo` internally to install the kernel image and all modules.

### Manual installation

#### Kernel image and in-tree modules

```bash
export INSTALL_MOD_PATH=/path/to/target/rootfs
make -C kernel/ install
```

This copies:
- `Image` to `$INSTALL_MOD_PATH/boot/`
- In-tree `.ko` files to `$INSTALL_MOD_PATH/lib/modules/<version>/`

#### OOT modules

```bash
export INSTALL_MOD_PATH=/path/to/target/rootfs
sudo -E make modules_install INSTALL_MOD_DIR=updates
```

OOT modules are installed under `$INSTALL_MOD_PATH/lib/modules/<version>/updates/`.

#### DTBs

Copy the compiled DTBs manually:

```bash
cp kernel-devicetree/generic-dts/dtbs/*.dtb /path/to/target/rootfs/boot/dtb/
```

---

## Quick Reference

```bash
# Full setup and build from scratch:
export CROSS_COMPILE=$PWD/compiler/bin/aarch64-buildroot-linux-gnu-
export KERNEL_SRC_DIR="kernel-6-12-y"
export INSTALL_MOD_PATH=/path/to/target/rootfs

./nvbuild.sh                  # build everything
./nvbuild.sh -i               # install to rootfs
```
