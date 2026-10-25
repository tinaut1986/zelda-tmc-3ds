/*--------------------------------------------------------------------------------
	This Source Code Form is subject to the terms of the Mozilla Public License,
	v. 2.0. If a copy of the MPL was not distributed with this file, You can
	obtain one at https://mozilla.org/MPL/2.0/.
--------------------------------------------------------------------------------*/
/* Minish Cap 3DS: devkitARM's arm-none-eabi/lib/3dsx.ld with the game's code
 * and data laid out first, at fixed addresses, for save states
 * (platform/3ds/source/save_state_3ds.c, docs/save-states.md).
 *
 * A state is a raw copy of the game's .data and .bss (the brackets
 * __ss_data_start..__ss_data_end and __ss_bss_start..__ss_bss_end), and the
 * game keeps pointers into its own code, constant tables and globals there.
 * Placing the game's objects (src/ and the game-side files of port/) first in
 * each section, from a pinned address, keeps all of those where they were
 * while only the port's own code (renderer, UI, platform) changes, so a state
 * survives such rebuilds. KEEP stops --gc-sections from dropping a different
 * set of game functions when port code starts or stops calling one. The
 * __ss_* symbols are hashed into each state; a build that moved any of them
 * refuses older states instead of loading dangling pointers.
 *
 * Code and constants: the game's go first in the code segment, which always
 * starts at 0x100000. Data: the data segment is pinned (the read-only segment
 * before it is padded up to it) and .bss is pinned inside it. The ASSERTs fire
 * when the port grows past a pin: raise it (that one build cannot load older
 * states). Keep the rest in sync with devkitARM. */
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
	code   PT_LOAD FLAGS(5) /* Read | Execute */;
	rodata PT_LOAD FLAGS(4) /* Read */;
	data   PT_LOAD FLAGS(6) /* Read | Write */;
}

SECTIONS
{
	/* =========== CODE section =========== */

	PROVIDE(__start__ = 0x100000);
	. = __start__;

	.text ALIGN(0x1000) :
	{
		/* .init */
		KEEP( *(.crt0) )
		KEEP( *(.init) )
		. = ALIGN(4);

		/* The game's code first (see the top of this file). */
		__ss_text_start = .;
		KEEP(EXCLUDE_FILE(*/port/* */libs/* */platform/*) *tmc-3ds.dir/*/src/*.o(.text .text.*))
		KEEP(EXCLUDE_FILE(*/port/ppu/* *port_second_screen* *port_m4a_backend* *port_m4a_mixdown* *generated_sounds_embed* *port_filter* *port_math* *port_text_render* *port_3ds_full_view_policy* *port_dump_state* *port_save.o *port_softslots* *port_bios.o *port_draw.o *port_zone_tiles* *port_neighbour_rooms*) */port/*.o(.text .text.*))
		__ss_text_end = .;
		/* The game's constant tables, also at a fixed place: in the code segment,
		 * whose start never moves. */
		__ss_rodata_start = .;
		KEEP(EXCLUDE_FILE(*/port/* */libs/* */platform/*) *tmc-3ds.dir/*/src/*.o(.rodata .rodata.* .roda))
		KEEP(EXCLUDE_FILE(*/port/ppu/* *port_second_screen* *port_m4a_backend* *port_m4a_mixdown* *generated_sounds_embed* *port_filter* *port_math* *port_text_render* *port_3ds_full_view_policy* *port_dump_state* *port_save.o *port_softslots* *port_bios.o *port_draw.o *port_zone_tiles* *port_neighbour_rooms*) */port/*.o(.rodata .rodata.* .roda))
		__ss_rodata_end = .;

		/* .text */
		*(.text)
		*(.text.*)
		*(.glue_7)
		*(.glue_7t)
		*(.stub)
		*(.gnu.warning)
		*(.gnu.linkonce.t*)
		. = ALIGN(4);

		/* .fini */
		KEEP( *(.fini) )
		. = ALIGN(4);
	} : code

	/* =========== RODATA section =========== */

	. = ALIGN(0x1000);

	.rodata :
	{
		*(.rodata)
		*(.roda)
		*(.rodata.*)
		*all.rodata*(*)
		*(.gnu.linkonce.r*)
		SORT(CONSTRUCTORS)
		. = ALIGN(4);
		__tdata_align = .;
		LONG (ALIGNOF(.tdata));
		. = ALIGN(4);
	} : rodata

	.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } : rodata
	__exidx_start = .;
	.ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } : rodata
	__exidx_end = .;

	/* 3dsxtool wants the segments back to back, so the read-only segment is
	 * padded up to the pinned start of the data segment (zeros in the file). */
	ASSERT(. <= 0x00640000, "3dsx.ld: read-only data passed the pinned .data (0x640000): raise it")
	__ss_pad_start = .;
	.ss_pad :
	{
		BYTE(0)
		. += 0x00640000 - __ss_pad_start - 1;
	} : rodata

	/* =========== DATA section =========== */

	. = 0x00640000;

	.data :
	{
		__ss_data_start = .;
		KEEP(EXCLUDE_FILE(*/port/* */libs/* */platform/*) *tmc-3ds.dir/*/src/*.o(.data .data.*))
		KEEP(EXCLUDE_FILE(*/port/ppu/* *port_second_screen* *port_m4a_backend* *port_m4a_mixdown* *generated_sounds_embed* *port_filter* *port_math* *port_text_render* *port_3ds_full_view_policy* *port_dump_state* *port_save.o *port_softslots* *port_bios.o *port_draw.o *port_zone_tiles* *port_neighbour_rooms*) */port/*.o(.data .data.*))
		. = ALIGN(4);
		__ss_data_end = .;
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d*)
		CONSTRUCTORS
		. = ALIGN(4);
	} : data

	.tdata : ALIGN(4)
	{
		__tdata_lma = .;
		*(.tdata)
		*(.tdata.*)
		*(.gnu.linkonce.td.*)
		. = ALIGN(4);
		__tdata_lma_end = .;
	} : data

	.tbss : ALIGN(4)
	{
		*(.tbss)
		*(.tbss.*)
		*(.gnu.linkonce.tb.*)
		*(.tcommon)
		. = ALIGN(4);
	} : data

	/*
    __tdata_align_val = ABSOLUTE(ALIGNOF(.tdata));
    __tdata_size = ABSOLUTE(SIZEOF(.tdata));
    __tbss_align = ABSOLUTE(ALIGNOF(.tbss));
    __tbss_size = ABSOLUTE(SIZEOF(.tbss));
	*/

	.preinit_array ALIGN(4) :
	{
		PROVIDE (__preinit_array_start = .);
		KEEP (*(.preinit_array))
		PROVIDE (__preinit_array_end = .);
	} : data

	.init_array ALIGN(4) :
	{
		PROVIDE (__init_array_start = .);
		KEEP (*(SORT(.init_array.*)))
		KEEP (*(.init_array))
		PROVIDE (__init_array_end = .);
	} : data

	.fini_array ALIGN(4) :
	{
		PROVIDE (__fini_array_start = .);
		KEEP (*(.fini_array))
		KEEP (*(SORT(.fini_array.*)))
		PROVIDE (__fini_array_end = .);
	} : data

	.ctors ALIGN(4) :
	{
		KEEP (*crtbegin.o(.ctors)) /* MUST be first -- GCC requires it */
		KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
		KEEP (*(SORT(.ctors.*)))
		KEEP (*(.ctors))
	} : data

	.dtors ALIGN(4) :
	{
		KEEP (*crtbegin.o(.dtors))
		KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
		KEEP (*(SORT(.dtors.*)))
		KEEP (*(.dtors))
	} : data

	/* Inside the data segment: a gap before .bss costs no file space. */
	ASSERT(. <= 0x00648000, "3dsx.ld: .data passed the pinned .bss (0x648000): raise it")
	. = 0x00648000;

	__bss_start__ = .;
	.bss ALIGN(4) :
	{
		__ss_bss_start = .;
		KEEP(EXCLUDE_FILE(*/port/* */libs/* */platform/*) *tmc-3ds.dir/*/src/*.o(.bss .bss.* COMMON))
		KEEP(EXCLUDE_FILE(*/port/ppu/* *port_second_screen* *port_m4a_backend* *port_m4a_mixdown* *generated_sounds_embed* *port_filter* *port_math* *port_text_render* *port_3ds_full_view_policy* *port_dump_state* *port_save.o *port_softslots* *port_bios.o *port_draw.o *port_zone_tiles* *port_neighbour_rooms*) */port/*.o(.bss .bss.* COMMON))
		. = ALIGN(4);
		__ss_bss_end = .;
		/* Empty when the object paths did not match (a checkout under a folder
		 * named port/, libs/ or platform/ would do it): fail the link. */
		ASSERT(__ss_bss_end - __ss_bss_start > 0x80000, "3dsx.ld: the game .bss bracket is nearly empty: check the object patterns");
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b*)
		*(COMMON)

		/* Reserve space for the TLS segment of the main thread.
		   We need (__tls_start - 8) to be aligned the same as .tdata, to account for
		   the 8-byte ARM TLS header. Since the header is not actually used for
		   ARM_TLS_LE32 relocation, we just fake it by subtracting 8 from the data
		   offset.
		 */
		. = 8 + ABSOLUTE(ALIGN(ABSOLUTE(. - 8), MAX(ALIGNOF(.tdata), ALIGNOF(.tbss))));
		__tls_start = .;
		. += SIZEOF(.tdata);

        /* Ensure the alignment of .tbss is accounted for in the total size,
           since SIZEOF(.tdata) doesn't include any padding before .tbss.
         */
        . = ALIGN(ALIGNOF(.tbss));
        . += SIZEOF(.tbss);
		__tls_end = .;
	} : data
	__bss_end__ = .;

	__end__ = ABSOLUTE(.) ;

	/* ==================
	   ==== Metadata ====
	   ================== */

	/* Discard sections that difficult post-processing */
	/DISCARD/ : { *(.group .comment .note) }

	/* Stabs debugging sections. */
	.stab          0 : { *(.stab) }
	.stabstr       0 : { *(.stabstr) }
	.stab.excl     0 : { *(.stab.excl) }
	.stab.exclstr  0 : { *(.stab.exclstr) }
	.stab.index    0 : { *(.stab.index) }
	.stab.indexstr 0 : { *(.stab.indexstr) }

	/* DWARF debug sections.
	   Symbols in the DWARF debugging sections are relative to the beginning
	   of the section so we begin them at 0. */

	/* DWARF 1 */
	.debug          0 : { *(.debug) }
	.line           0 : { *(.line) }

	/* GNU DWARF 1 extensions */
	.debug_srcinfo  0 : { *(.debug_srcinfo) }
	.debug_sfnames  0 : { *(.debug_sfnames) }

	/* DWARF 1.1 and DWARF 2 */
	.debug_aranges  0 : { *(.debug_aranges) }
	.debug_pubnames 0 : { *(.debug_pubnames) }

	/* DWARF 2 */
	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }
	.debug_macinfo  0 : { *(.debug_macinfo) }
}
