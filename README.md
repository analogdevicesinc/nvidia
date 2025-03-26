# Drivers for NVIDIA Jetson

## Supported projects

| Project 	| Documentation 	| Release Tag   |
|---------	| --------------	| ------------- |
| gmsl    	| [Here][doc-0] 	| jetson_35.3.1 |

[doc-0]: https://github.com/analogdevicesinc/nvidia/tree/gmsl/main

## Getting started

For the initial flashing process, there are multiple options:

- Use the [NVIDIA SDK Manager](https://developer.nvidia.com/sdk-manager) to flash your board.
- Follow the [NVIDIA Getting Started Guide](https://developer.nvidia.com/embedded/learn/getting-started-jetson) for your specific board. This will help you set up the board using a flashing program to write a microSD Card.
- Use [Jetson Linux](https://developer.nvidia.com/embedded/jetson-linux-archive) to flash your board.

## Further instructions

Further in this guide, we will use some scripts that we provide in this repo.

To be able follow along, we recommend cloning this repo in a location of your choice by running the following command.

`git clone https://github.com/analogdevicesinc/nvidia --single-branch -b main nvidia`

The scripts depend on some other Linux tools. Install those by running the following command.

`sudo apt install build-essential bc rsync`

## Further setup

To make development easier, we recommend doing the following modifications to your freshly-flashed board.

To run these commands, you need to either have a monitor, keyboard, and mouse, connected to your board, and to open the Terminal app, or be connected to your board either through SSH, or through the UART debug console.

Your board must be accessible via SSH for some of the steps in this guide, so it is better to make sure of that now.

### Set a root user password

1. Run the following command and fill the necessary prompts.

`sudo passwd root`

### Permit root user password login through SSH

1. Edit the `/etc/ssh/sshd_config` file in your favorite editor.
2. Uncomment the `#PermitRootLogin prohibit-password` line by removing the `#` at the start of it.
3. Replace the `prohibit-password` value with `yes`.
4. Restart the SSH server by running `sudo systemctl restart sshd`.

### Enable automatic login

1. Open the Settings app.
2. Go to the Users settings page.
3. Unlock to change settings.
4. Turn on Automatic Login.

Alternatively, you can also put the following lines into the `[daemon]` section of the `/etc/gdm3/custom.conf` file, where user is your username.
```
AutomaticLoginEnable=True
AutomaticLogin=user
```

### Disable screen blanking

1. Open the Settings app.
2. Go to the Power settings page.
3. Switch the Blank Screen value to Never.

### Setup VNC server

1. Open the Settings app.
2. Go to the Sharing settings page.
3. Open the Screen Sharing menu.
4. Turn on Screen Sharing, set a password, and check the Ethernet connection to use.

### Disable network boot

Network boot is enabled by default and it slows down the boot process when booting from a microSD Card.

To disable it, make sure your keyboard is connected to the board and press `Esc` at the start of the board's boot sequence.

1. Use the arrow keys to navigate to the `Boot Maintainance Manager` entry, and press `Enter`.
2. Enter Boot Options.
3. Enter `Change Boot Order`.
4. Press `Enter` to entry the order change menu.
5. Navigate to the `UEFI SD Device` entry, and use the `+` key to move it at the top of the list.
6. Press `Enter` when you're finished.
7. Press `F10` to save your changes.
8. Press `Esc` to go back to the previous page until you reach the main page.
9. Select `Continue` to continue the boot process.

## Setting up the development environment

Download a [Jetson Linux Driver Package (BSP)](https://developer.nvidia.com/embedded/jetson-linux-archive). Depending on the `Release Tag` of the project you want to use, you need to download a different version of the Driver Package.

Extract the Driver Package in a directory of your choice.

Open a terminal in the directory where you extracted the Driver Package.

Sync the sources by running the `./source_sync.sh` script.

When prompted for a tag name, enther the `Release Tag` value from the table in each project's documentation under the [Supported projects](#supported-projects) section.

Once the script finishes, your development environment should be ready.

For more information about the development environment, you can check the `Jetson Linux Developer Guide` found in the page of the Jetson Linux release of your choice.

## Working with the source code

After you set up your development environment, you have multiple ways of working the source code.

### Repo structure

If you look at the branches of this repo, you will notice that there are multiple branches, which are named accordingly to each project's documentation under the [Supported projects](#supported-projects) section. The branch names also contain the paths to the repos inside the Jetson Linux tree that we have modified.

### Syncing from the repos into the Jetson Linux tree

To do this, run the following script (provided in this repo) specifying the path to the Jetson Linux directory, a remote name of your choice (we recomment using `adi`) and the remote url at which this repo lives. You also need to specify the `Project` and `Release Tag` values from the table in the [Supported projects](#supported-projects) section.

`./sync.sh ../Linux_for_Tegra_R35.3.1 adi https://github.com/analogdevicesinc/nvidia.git gmsl jetson_35.3.1`

### Applying patches to the Jetson Linux tree

> :warning: Use this method only if you have a **patches-jetson_35.3.1** archive provided by ADI or you created one following **Extracting patches from the Jetson Linux tree**

Applying patches is useful if you intend to combine patches from multiple manufacturers.

In this way, the patches provided by us can be applied alongside the patches provided by other manufacturers.

To do this, run the following script (provided in this repo) specifying the path to the Jetson Linux directory, and the patches directory matching the `Patches Directory` value from the table in each project's documentation under the [Supported projects](#supported-projects) section.

`./apply-patches.sh ../Linux_for_Tegra_R35.3.1/ ../nvidia-gmsl/patches-jetson_35.3.1`

### Resetting the Jetson Linux tree to NVIDIA state

You can also use the previous script to return to the NVIDIA state of the Jetson Linux tree.

To do this, run the following script (provided in this repo) specifying the path to the Jetson Linux directory, the remote name of the NVIDIA repos (usually `origin`) and the `Release Tag` which you want to sync.

`./sync.sh ../Linux_for_Tegra_R35.3.1 origin jetson_35.3.1`

### Pushing your changes to a repo

You can push your changes to a repo following the same structure as the one we provide.

To do this, run the following script (provided in this repo) specifying the path to the Jetson Linux directory, the path of the repo that you want to push, the remote name that you want to push to, a project name and a tag name.

`./push.sh ../Linux_for_Tegra_R35.3.1 ../Linux_for_Tegra_R35.3.1/sources/kernel/kernel-5.10 adi gmsl jetson_35.3.1`

You can also force-push using the same script by specifying the `--force` (or `-f`) argument.

### Extracting patches from the Jetson Linux tree

You can extract patches in a similar way to the ones we provide.

To do this, run the following script (provided in this repo) specifying the path to the Jetson Linux directory, the base remote name and tag name (against which the patches will be generated, which means the patches will contain all the code from that tag to the current head of the repos) and a directory into which to put the patches.

`./extract-patches.sh ../Linux_for_Tegra_R35.3.1 origin jetson_35.3.1 ../nvidia-gmsl/patches-jetson_35.3.1`

## Building

To start building the source code, you can follow the `Kernel Customization` guide inside the `Jetson Linux Developer Guide`, but we also provide scripts to help you build and copy your newly-built custom kernel to the board.

### Setting up the toolchain

Follow [this guide](https://docs.nvidia.com/jetson/archives/r35.3.1/DeveloperGuide/text/AT/JetsonLinuxToolchain.html).

### Build

Open a terminal inside the `sources/kernel/kernel-5.10` subdirectory of the Jetson Linux tree.

After this, run the following script (provided in this repo).

You have to specify the full path to the script, since it is only contained in this repo, and not in the Jetson Linux tree.

`~/nvidia/build.sh`

This will build the kernel, device trees and modules.

The build process will create two subdirectories in `sources` folder of Jetson Linux tree. One is `kernel_out` containing the kernel `Image` and `dtbs`. The other is `modules_out` containing the linux kernel modules.

### Device trees

We support different project configurations by providing different device tree blobs (DTBs).

To find out which device tree blob you need to use, check out each project's documentation under the [Supported projects](#supported-projects) section.

To switch to another device tree blob, run the following script (provided in this repo).

`~/nvidia/set-dtb.sh tegra234-p3767-0003-p3768-0000-a0.dtb 192.168.0.103`

The `tegra234-p3767-0003-p3768-0000-a0.dtb` parameter must be replaced with the device tree blob of your choice.

The `192.168.0.103` parameter must be replaced with the IP of your board.

### Copying

Open a terminal inside the `sources/kernel/kernel-5.10` subdirectory of the Jetson Linux tree.

After this, run the following script (provided in this repo).

`~/nvidia/copy-to-board.sh 192.168.0.103`

The `192.168.0.103` parameter must be replaced with the IP of your board.

You must be connected to the same network as your board and have the root user accessible via SSH, as recommended in the [Further setup](#further-setup) section.

The script will copy the kernel, device trees and modules to the board and will ask you whether to reboot.
