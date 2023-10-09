# Getting started

[TOC]

## Getting the source code

```shell
git clone --recurse-submodules https://git.trustedfirmware.org/hafnium/hafnium.git && { cd hafnium && f="$(git rev-parse --git-dir)"; curl -Lo "$f/hooks/commit-msg" https://review.trustedfirmware.org/tools/hooks/commit-msg && { chmod +x "$f/hooks/commit-msg"; git submodule --quiet foreach "cp \"\$toplevel/$f/hooks/commit-msg\" \"\$toplevel/$f/modules/\$path/hooks/commit-msg\""; }; }
```

To upload a commit for review:

```shell
git push origin HEAD:refs/for/master
```

Browse source at https://review.trustedfirmware.org/plugins/gitiles/. Review CLs
at https://review.trustedfirmware.org/.

See details of [how to contribute](../CONTRIBUTING.md).

## Compiling the hypervisor

Install prerequisites:

```shell
sudo apt install make libssl-dev flex bison python3 python3-serial python3-pip
pip3 install fdt
```

By default, the hypervisor is built with clang for a few target platforms along
with tests. Each project in the `project` directory specifies a root
configurations of the build. Adding a project is the preferred way to extend
support to new platforms. The target project that is built is selected by the
`PROJECT` make variable, the default project is 'reference'.

```shell
make PROJECT=<project_name>
```

The compiled image can be found under `out/<project>`, for example the QEMU
image is at `out/reference/qemu_aarch64_clang/peregrine.bin`.

## Running on QEMU

You will need at least version 2.9 for QEMU. The following command line can be
used to run Hafnium on it:

```shell
qemu-system-aarch64 -M virt,gic_version=3 -cpu cortex-a57 -nographic -machine virtualization=true -kernel out/reference/qemu_aarch64_clang/peregrine.bin
```

Though it is admittedly not very useful because it doesn't have any virtual
machines to run.

Next, you need to create a manifest which will describe the VM to Hafnium.
Follow the [Manifest](Manifest.md) instructions and build a DTB with:

```
/dts-v1/;

/ {
	hypervisor {
		compatible = "hafnium,hafnium";
		vm1 {
			debug_name = "Linux VM";
			kernel_filename = "vmlinuz";
			ramdisk_filename = "initrd.img";
		};
	};
};
```

Follow the [Hafnium RAM disk](HafniumRamDisk.md) instructions to create an
initial RAM disk for Hafnium with Linux as the primary VM.

The following command line will run Hafnium, with the RAM disk just created,
which will then boot into the primary Linux VM:

```shell
qemu-system-aarch64 -M virt,gic_version=3 -cpu cortex-a57 -nographic -machine virtualization=true -kernel out/reference/qemu_aarch64_clang/peregrine.bin -initrd initrd.img -append "rdinit=/sbin/init"
```

## Running tests

After building, presubmit tests can be run with the following command line:

```shell
./kokoro/test.sh
```

Read about [testing](Testing.md) for more details about the tests.
