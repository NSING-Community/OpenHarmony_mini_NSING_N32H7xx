/* Linker-script fragments that are easy to forget. Merge into the vendor .ld. */

ENTRY(Reset_Handler)

MEMORY
{
    /* Sizes are chip facts: take them from the datasheet, not from a guess. */
    FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    RAM     (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Kernel heap. Too small -> LOS_KernelInit fails when a contiguous block is
 * needed. Verify with: nm <elf> | grep __heap  (heap is NOLOAD, so the file
 * size will NOT change when you edit this value). */
_Min_Heap_Size  = 0x1C000;
_Min_Stack_Size = 0x1000;

SECTIONS
{
    /* Vector table must be aligned to at least pow2(vectors * 4):
     * 432 vectors -> 1728 B -> 2048. Too small -> Type=11 on the first
     * peripheral access after the kernel starts. */
    .vector : ALIGN(0x800)
    {
        KEEP(*(.vector))
    } > FLASH

    /* HDF driver table: HDF_INIT() writes pointers here. Without KEEP the
     * section is collected by --gc-sections and drivers silently disappear.
     * Each driver costs 4 bytes on a 32-bit target. */
    .hdf.driver :
    {
        . = ALIGN(4);
        _hdf_drivers_start = .;
        KEEP(*(.hdf.driver))
        _hdf_drivers_end = .;
        . = ALIGN(4);
    } > FLASH

    /* initcall layers used by SYS_RUN / SYS_INIT / MODULE_INIT */
    .zinitcall.run0.init : { KEEP(*(.zinitcall.run0.init)) } > FLASH
    .zinitcall.run1.init : { KEEP(*(.zinitcall.run1.init)) } > FLASH
    .zinitcall.run2.init : { KEEP(*(.zinitcall.run2.init)) } > FLASH
    .zinitcall.run3.init : { KEEP(*(.zinitcall.run3.init)) } > FLASH
    .zinitcall.run4.init : { KEEP(*(.zinitcall.run4.init)) } > FLASH
}
