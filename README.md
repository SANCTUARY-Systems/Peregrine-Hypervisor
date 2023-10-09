# Hafnium

Hafnium is a hypervisor, initially supporting aarch64 (64-bit Armv8 CPUs).

Get in touch and keep up-to-date at
[hafnium@lists.trustedfirmware.org](https://lists.trustedfirmware.org/mailman/listinfo/hafnium).
See feature requests and bugs on our
[bug dashboard](https://developer.trustedfirmware.org/project/21/item/view/67/).

## Getting started

`git submodule update --init --recursive`

## Hafium Quick Start

#### Prepare 
1. Download and install required [Toolchain](https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-aarch64-none-elf.tar.xz?revision=79f65c42-1a1b-43f2-acb7-a795c8427085&la=en&hash=3BEE623628664E6E1736ABBE7CE5AD76B65B51EA).
2. Install a aarch64-linux-gnu GCC toolchain with static glibc.

### Build Linux
1. Download Linux Kernel: https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.0.21.tar.xz 
2. Patch Kernel with PATCH-5.5-102-170-scripts-dtc-Remove-redundant-YYLOC-global-declaration.patch (git apply) inside this repo
3. Build Kernel: make defconfig && make. Make sure ARCH and CROSS_COMPILE are set correctly in environment (`export ARCH=arm64 CROSS_COMPILE=aarch64-none-elf- && make defconfig && make -j4`).
4. Result Kernel is arch/arm64/boot/Image

### Build Initramfs
1. Download Busybox: `https://busybox.net/downloads/busybox-1.33.1.tar.bz2` (`curl https://busybox.net/downloads/busybox-1.33.1.tar.bz2 | bunzip2 | tar -xv`)
2. Copy .config from busybox directory inside repo.
3. Build Busybox: `make && make install`. Make sure ARCH and CROSS_COMPILE are set correctly in environment (`export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- && make && make install`). Please note the toolchain is different in comparison to the other programs!
4. A directory _install is created. Inside it, create directories etc, proc and sys.
5. (optional, not recommended, extremly slows down boot) Copy Linux modules to _install directory: Inside Linux directory, 
`exec ARCH=arm64 CROSS_COMPILE=aarch64-none-elf- INSTALL_MOD_PATH=/path/to/busybox-1.33.1/_install make modules_install`.
6. Write an init script and copy it to _install/init. An example is inside busybox directory inside this repo.
7. Pack the initramfs: Inside the _install directory, call `find . | cpio -o -H newc | gzip > initramfs.img`


### Build Hafnium
1. Apply patch "" to hafnium/third_party/linux (git apply)
3. Exec `make`.
4. Result is out/reference/aem_v8a_fvp_clang/peregrine.bin

#### Create Hafnium manifest
Hafnium is configured using a manifest device tree. It looks as follows:
```
/dts-v1/;

/ {
	hypervisor {
		compatible = "hafnium,hafnium";
		vm1 {
			debug_name = "Linux VM";
			kernel_filename = "Image";
			ramdisk_filename = "initramfs.img";
		};
	};
};
```

`kernel_filename` references the Kernel file inside Hafnium Ramdisk, `ramdisk_filename` references the Linux Initramfs inside the Hafnium Ramdisk. Please note the Linux Initramfs must not be prepared for Das U-Boot!

Please compile the manifest before using it in Hafnium: `dtc -O dtb -I dts -o manifest.dtb manifest.dts`

#### Build Hafnium Ramdisk
A Hafnium ramdisk consists of a device tree which specifies VM parameters, a Linux kernel, its initramfs, and other VM images (optional).
`printf "mainfest.dtb\nImage\ninitrd.img\n" | cpio > initrd.img`

### Build Trusted Firmware-A
1. Download TFA: `git clone https://sgit.trust.informatik.tu-darmstadt.de/sanctuary/trusted-firmware-a.git`
2. `make CROSS_COMPILE=aarch64-none-elf- PLAT=fvp ARM_ARCH_MINOR=5 CTX_INCLUDE_EL2_REGS=1 BRANCH_PROTECTION=1 CTX_INCLUDE_PAUTH_REGS=1 ARM_PRELOADED_DTB_BASE=0x82000000 ARM_LINUX_KERNEL_AS_BL33=1 PRELOADED_BL33_BASE=0x80000000 FVP_HW_CONFIG_DTS=fdts/fvp-base-gicv3-psci-1t.dts  RESET_TO_BL31=1 all fip`
3. Result is build/fvp/release/bl31.bin.

#### Fixup DTB for Hafnium
1. build/fvp/release/fdts contains a file fvp-base-gicv3-psci-1t.pre.dts. Inside the `chosen` part, add following:
```
chosen {
   bootargs = "";
   stdout-path = "serial0:115200n8";
   linux,initrd-start = <0x84000000>;
   linux,initrd-end = <0x86000000>;
};
```
Please change the initrd-end address so the difference between initrd-start and initrd-end is at least as large as the Hafnium initrd.
2. Build dts: `dtc -O dtb -I dts -o fvp-base-gicv3-psci-1t.dtb fvp-base-gicv3-psci-1t.pre.dts`
3. Result is the fvp-base-gicv3-psci-1t.dtb.

## Run FVP
1. Download ARM Base FVP: <https://silver.arm.com/download/download.tm?pv=4849271&p=3042387>
2. Install
3. *Change paths to files if neccessary!*, execute:
```
Base_RevC_AEMvA_pkg/models/Linux64_GCC-6.4/FVP_Base_RevC-2xAEMvA -C pctl.startup=0.0.0.0 -C cluster0.NUM_CORES=4 -C cluster1.NUM_CORES=4 -C bp.secure_memory=1 -C cluster0.has_arm_v8-5=1 -C cluster1.has_arm_v8-5=1 -C pci.pci_smmuv3.mmu.SMMU_AIDR=2 -C pci.pci_smmuv3.mmu.SMMU_IDR0=0x0046123B -C pci.pci_smmuv3.mmu.SMMU_IDR1=0x00600002 -C pci.pci_smmuv3.mmu.SMMU_IDR3=0x1714 -C pci.pci_smmuv3.mmu.SMMU_IDR5=0xFFFF0472 -C pci.pci_smmuv3.mmu.SMMU_S_IDR1=0xA0000002 -C pci.pci_smmuv3.mmu.SMMU_S_IDR2=0 -C pci.pci_smmuv3.mmu.SMMU_S_IDR3=0 -C cluster0.has_branch_target_exception=1 -C cluster1.has_branch_target_exception=1 -C cluster0.restriction_on_speculative_execution=2 -C cluster1.restriction_on_speculative_execution=2 --data cluster0.cpu0=hafnium/out/reference/aem_v8a_fvp_clang/peregrine.bin@0x80000000 --data cluster0.cpu0=hafnium-initrd.img@0x84000000 --data cluster0.cpu0=Trusted-Firmware-A/build/fvp/release/fdts/fvp-base-gicv3-psci-hf.dtb@0x82000000 --data cluster0.cpu0=Trusted-Firmware-A/build/fvp/release/bl31.bin@0x04001000 -C cluster0.cpu0.RVBAR=0x04001000 -C cluster0.cpu1.RVBAR=0x04001000 -C cluster0.cpu2.RVBAR=0x04001000 -C cluster0.cpu3.RVBAR=0x04001000 -C cluster1.cpu0.RVBAR=0x04001000 -C cluster1.cpu1.RVBAR=0x04001000 -C cluster1.cpu2.RVBAR=0x04001000 -C cluster1.cpu3.RVBAR=0x04001000
```
4. Linux boots, ash will come up.

## Run the test cases

To execute the test cases, run:

`./kokoro/build.sh`

Provided that all tests pass, a code-coverage report is generated in the `out/sanctuary_test/coverage` directory. \
To open the contents of the coverage report in a web browser, run:

`xdg-open $PWD/out/sanctuary_test/index.html`

## Building and Contributing
To jump in and build Hafnium, follow the
[getting started](docs/GettingStarted.md) instructions.

If you want to contribute to the project, see details of
[how we accept contributions](CONTRIBUTING.md).

## Documentation

More documentation is available on:

*   [Hafnium architecture](docs/Architecture.md)
*   [Code structure](docs/CodeStructure.md)
*   [Hafnium test infrastructure](docs/Testing.md)
*   [Running Hafnium under the Arm Fixed Virtual Platform](docs/FVP.md)
*   [How to build a RAM disk containing VMs for Hafnium to run](docs/HafniumRamDisk.md)
*   [Building Hafnium hermetically with Docker](docs/HermeticBuild.md)
*   [The interface Hafnium provides to VMs](docs/VmInterface.md)
*   [Scheduler VM expectations](docs/SchedulerExpectations.md)
*   [Hafnium coding style](docs/StyleGuide.md)
