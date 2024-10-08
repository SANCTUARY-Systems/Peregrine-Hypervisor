/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

/*
 * Code will start running at this symbol which is places at the start of the
 * image.
 */
ENTRY(entry)

/*
 * The following would be useful to check that .init code is not called back
 * into once it has completed but it isn't supported by ld.lld.
 *
 * NOCROSSREFS_TO(.init .text)
 */

SECTIONS
{
	/*
	 * Set the image origin to a platform specific address. The images are
	 * relocatable but some platforms, e.g. QEMU, load to the same address
	 * and it makes debugging easier if the addresses match the symbols.
	 */
	. = ORIGIN_ADDRESS;

	/*
	 * Collect together the code. This is page aligned so it can be mapped
	 * as executable-only.
	 */
	text_begin = .;
	.init : {
		*(.init.entry)
		*(.init.*)
	}
	.text : {
		*(.text.*)
	}
	text_size = ABSOLUTE(. - text_begin);
	. = ALIGN(4096);
	text_end = .;

	/*
	 * Collect together read-only data including relocations at the end
	 * which are applied by the entry code. This is page aligned so it can
	 * be mapped as read-only and non-executable.
	 */
	. = ALIGN(4096);
	rodata_begin = .;
	.rodata : {
		*(.rodata.*)
	}
	/*
	 * .rela contains Elf64_Rela entries which contain 8-byte fields so
	 * should be 8-byte aligned.
	 */
	. = ALIGN(8);
	rela_begin = .;
	.rela : {
		*(.rela.*)
	}
	rela_end = .;
	/*
	 * The linker doesn't allow .dynsym and .dynstr to be discarded, see
	 * /DISCARD/ below, so make sure they don't get in the way.
	 */
	.dynsym : {
		*(.dynsym.*)
	}
	.dynstr : {
		*(.dynstr.*)
	}
	/*
	 * The hftest framework adds test descriptors in the .hftest section
	 * which is examined at runtime to discover the available tests. The
	 * input sections are named after the test they include so sorting here
	 * means they are stored sorted by the name of the test suite and then
	 * by test case names. To ensure tests aren't accidentally included in
	 * images that are not meant to have them, the assertion checks for a
	 * marker to signal tests are allowed.
	 */
	. = ALIGN(8);
	hftest_begin = .;
	.hftest : {
		KEEP(*(SORT(.hftest.*)))
	}
	hftest_end = .;
	ASSERT((SIZEOF(.hftest) == (DEFINED(hftest_enable) ? SIZEOF(.hftest) : 0)),
	       "Error: Image includes .hftest section but not HFTEST_ENABLE().")
	rodata_size = ABSOLUTE(. - rodata_begin);
	. = ALIGN(4096);
	rodata_end = .;

	/*
	 * A platform may choose to link blobs such as the FDT or the initrd
	 * into the image rather than having them loaded separately. These are
	 * placed at the end of the image and will not be mapped automatically
	 * on boot so they can be treated as if they were loaded as separate
	 * blobs. They are page aligned so they can be mapped individually.
	 *
	 * TODO: remove this when the loader can reliably deliver both the
	 * binary and a separate blob for the initrd.
	 */
	. = ALIGN(4096);
	initrd_begin = .;
	.initrd : {
		KEEP(*(.plat.initrd))
	}
	initrd_end = .;
	. = ALIGN(4096);
	fdt_begin = .;
	.fdt : {
		KEEP(*(.plat.fdt))
	}
	fdt_end = .;

	/*
	 * Collect together the read-write data including .bss at the end which
	 * will be zero'd by the entry code. This is page aligned so it can be
	 * mapped as non-executable.
	 */
	. = ALIGN(4096);
	data_begin = .;
	.data : {
		*(.data)
	}
	/*
	 * Global offset table used for relocations. This is where relocation
	 * fix-ups are applied.
	 */
	.got : {
		*(.got.*)
	}
	/*
	 * The linker doesn't allow .dynamic to be discarded, see /DISCARD/
	 * below, so make sure it doesn't get in the way.
	 */
	.dynamic : {
		*(.dynamic.*)
	}
	/* Everything beyond this point will not be included in the binary. */
	bin_end = .;
	/* The entry point code assumes that .bss is 16-byte aligned. */
	. = ALIGN(16);
	bss_begin = .;
	.bss : {
		*(.bss*)
		*(COMMON)
	}
	. = ALIGN(16);
	bss_end = .;
	data_size = ABSOLUTE(. - data_begin);
	. = ALIGN(4096);
	data_end = .;

	/*
	 * Remove unused sections from the image.
	 */
	/DISCARD/ : {
		/* The image loads itself so doesn't need these sections. */
		/* ld.lld doesn't allow these to be discarded.
		*(.dynsym)
		*(.dynstr)
		*(.dynamic)
		*/
		*(.gnu.hash)
		*(.hash)
		*(.interp)
	}

	/*
	 * Make note of some useful values.
	 */

	/* Note the first page not used in the image. */
	. = ALIGN(4096);
	image_end = .;

	/*
	 * Calculate sizes of the binary file and image loaded into memory as
	 * well as the text, read-only and read-write data sections.
	 */
	bin_size = ABSOLUTE(bin_end - ORIGIN_ADDRESS);
	image_size = ABSOLUTE(image_end - ORIGIN_ADDRESS);
}
