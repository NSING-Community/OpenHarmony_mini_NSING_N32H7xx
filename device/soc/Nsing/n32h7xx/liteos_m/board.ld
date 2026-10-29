/*********************************************************************************************************
     Copyright (c) 2023, Nations Technologies Inc.
 
     All rights reserved.

     This software is the exclusive property of Nations Technologies Inc. (Hereinafter 
 referred to as NATIONS). This software, and the product of NATIONS described herein 
 (Hereinafter referred to as the Product) are owned by NATIONS under the laws and treaties
 of the People's Republic of China and other applicable jurisdictions worldwide.

     NATIONS does not grant any license under its patents, copyrights, trademarks, or other 
 intellectual property rights. Names and brands of third party may be mentioned or referred 
 thereto (if any) for identification purposes only.

     NATIONS reserves the right to make changes, corrections, enhancements, modifications, and 
 improvements to this software at any time without notice. Please contact NATIONS and obtain 
 the latest version of this software before placing orders.

     Although NATIONS has attempted to provide accurate and reliable information, NATIONS assumes 
 no responsibility for the accuracy and reliability of this software.
 
     It is the responsibility of the user of this software to properly design, program, and test 
 the functionality and safety of any application made of this information and any resulting product. 
 In no event shall NATIONS be liable for any direct, indirect, incidental, special,exemplary, or 
 consequential damages arising in any way out of the use of this software or the Product.

     NATIONS Products are neither intended nor warranted for usage in systems or equipment, any
 malfunction or failure of which may cause loss of human life, bodily injury or severe property 
 damage. Such applications are deemed, "Insecure Usage".

     All Insecure Usage shall be made at user's risk. User shall indemnify NATIONS and hold NATIONS 
 harmless from and against all claims, costs, damages, and other liabilities, arising from or related 
 to any customer's Insecure Usage.

     Any express or implied warranty with regard to this software or the Product, including,but not 
 limited to, the warranties of merchantability, fitness for a particular purpose and non-infringement
 are disclaimed to the fullest extent permitted by law.

     Unless otherwise explicitly permitted by NATIONS, anyone may not duplicate, modify, transcribe
 or otherwise distribute this software for any purposes, in whole or in part.

     NATIONS products and technologies shall not be used for or incorporated into any products or systems
 whose manufacture, use, or sale is prohibited under any applicable domestic or foreign laws or regulations. 
 User shall comply with any applicable export control laws and regulations promulgated and administered by 
 the governments of any countries asserting jurisdiction over the parties or transactions.
 ************************************************************************************************************/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = 0x24020000;    /* AXI SRAM: 128K SRAM1 + 384K pool (OTP TCM_SIZE=0x18, proven by CamQrDecoder demo). DO NOT assume 1152K. */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x17000;     /* 92KB. The 128KB AXI SRAM has to hold .data/.bss as well, and
                                 those grow by a few KB once the XTS suites are linked in,
                                 so the heap cannot claim the whole region. */
_Min_Stack_Size = 0x1000;     /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
AXI_SRAM (xrw)     : ORIGIN = 0x24000000, LENGTH = 0x20000       /* 128K of the 384K AXI SRAM on this part (rest left for CM4/future use). */
AHB_SRAM  (xrw)    : ORIGIN = 0x30000000, LENGTH = 0x58000       /* 352K; real base is 0x30000000 per n32h7xx.h AHBSRAM_BASE */
FLASH (rx)     : ORIGIN = 0x15000000, LENGTH = 0x1E0000          /* 2*1024K - 128K */
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);

    /* HDF driver table. HDF_INIT() places each HdfDriverEntry pointer into
       .hdf.driver; the HDF core walks [_hdf_drivers_start,_hdf_drivers_end)
       at DeviceManagerStart(). KEEP is required because nothing references
       these entries directly, so --gc-sections would otherwise drop them. */
    _hdf_drivers_start = .;
    KEEP(*(.hdf.driver))
    _hdf_drivers_end = .;
    . = ALIGN(4);
  } >FLASH

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into AXI_SRAM, load LMA copy after code */
  .data : 
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >AXI_SRAM AT> FLASH

  .zInit :
  {
    __zinitcall_bsp_start = .;
    KEEP (*(.zinitcall.bsp0.init))
    KEEP (*(.zinitcall.bsp1.init))
    KEEP (*(.zinitcall.bsp2.init))
    KEEP (*(.zinitcall.bsp3.init))
    KEEP (*(.zinitcall.bsp4.init))
    __zinitcall_bsp_end = .;
    . = ALIGN(4);
    __zinitcall_device_start = .;
    KEEP (*(.zinitcall.device0.init))
    KEEP (*(.zinitcall.device1.init))
    KEEP (*(.zinitcall.device2.init))
    KEEP (*(.zinitcall.device3.init))
    KEEP (*(.zinitcall.device4.init))
    __zinitcall_device_end = .;
    . = ALIGN(4);
    __zinitcall_core_start = .;
    KEEP (*(.zinitcall.core0.init))
    KEEP (*(.zinitcall.core1.init))
    KEEP (*(.zinitcall.core2.init))
    KEEP (*(.zinitcall.core3.init))
    KEEP (*(.zinitcall.core4.init))
    __zinitcall_core_end = .;
    . = ALIGN(4);
    __zinitcall_sys_service_start = .;
    KEEP (*(.zinitcall.sys.service0.init))
    KEEP (*(.zinitcall.sys.service1.init))
    KEEP (*(.zinitcall.sys.service2.init))
    KEEP (*(.zinitcall.sys.service3.init))
    KEEP (*(.zinitcall.sys.service4.init))
    __zinitcall_sys_service_end = .;
    . = ALIGN(4);
    __zinitcall_sys_feature_start = .;
    KEEP (*(.zinitcall.sys.feature0.init))
    KEEP (*(.zinitcall.sys.feature1.init))
    KEEP (*(.zinitcall.sys.feature2.init))
    KEEP (*(.zinitcall.sys.feature3.init))
    KEEP (*(.zinitcall.sys.feature4.init))
    __zinitcall_sys_feature_end = .;
    . = ALIGN(4);
    __zinitcall_run_start = .;
    KEEP (*(.zinitcall.run0.init))
    KEEP (*(.zinitcall.run1.init))
    KEEP (*(.zinitcall.run2.init))
    KEEP (*(.zinitcall.run3.init))
    KEEP (*(.zinitcall.run4.init))
    __zinitcall_run_end = .;
    . = ALIGN(4);
    __zinitcall_app_service_start = .;
    KEEP (*(.zinitcall.app.service0.init))
    KEEP (*(.zinitcall.app.service1.init))
    KEEP (*(.zinitcall.app.service2.init))
    KEEP (*(.zinitcall.app.service3.init))
    KEEP (*(.zinitcall.app.service4.init))
    __zinitcall_app_service_end = .;
    . = ALIGN(4);
    __zinitcall_app_feature_start = .;
    KEEP (*(.zinitcall.app.feature0.init))
    KEEP (*(.zinitcall.app.feature1.init))
    KEEP (*(.zinitcall.app.feature2.init))
    KEEP (*(.zinitcall.app.feature3.init))
    KEEP (*(.zinitcall.app.feature4.init))
    __zinitcall_app_feature_end = .;
    . = ALIGN(4);
    __zinitcall_test_start = .;
    KEEP (*(.zinitcall.test0.init))
    KEEP (*(.zinitcall.test1.init))
    KEEP (*(.zinitcall.test2.init))
    KEEP (*(.zinitcall.test3.init))
    KEEP (*(.zinitcall.test4.init))
    __zinitcall_test_end = .;
    . = ALIGN(4);
    __zinitcall_exit_start = .;
    KEEP (*(.zinitcall.exit0.init))
    KEEP (*(.zinitcall.exit1.init))
    KEEP (*(.zinitcall.exit2.init))
    KEEP (*(.zinitcall.exit3.init))
    KEEP (*(.zinitcall.exit4.init))
    __zinitcall_exit_end = .;
    . = ALIGN(4);
  } >FLASH

  /*
   * LiteOS-M RAM vector table (g_hwiForm).
   *
   * The kernel declares it as:
   *     LITE_OS_SEC_VEC                                        -> section(".vector")
   *     __attribute__((aligned(LOSCFG_ARCH_HWI_VECTOR_ALIGN))) g_hwiForm[OS_VECTOR_CNT]
   * HalHwiInit() then does SCB->VTOR = (UINT32)g_hwiForm.
   *
   * Cortex-M7 requires VTOR to be aligned to the vector table size rounded up
   * to a power of two (minimum 128 bytes).  With OS_VECTOR_CNT = 16 + 416 = 432
   * entries -> 1728 bytes -> the required alignment is 2048 (0x800).
   *
   * If .vector is left as an orphan section the linker may honour only the
   * default 0x100 from los_config.h and place it at a 256-byte boundary (e.g.
   * 0x24000100).  The core then ignores the low VTOR bits when dispatching an
   * exception, fetches a garbage handler, and raises an imprecise bus fault
   * (OS_EXC_BF_IMPRECISERR, Type=11, FaultAddr=0xABABABAB) as soon as the kernel
   * enables BUSFAULT/MEMFAULT.  Hence this explicit, ALIGN(0x800) definition.
   *
   * Keep the ALIGN value >= the power-of-two rounding of OS_VECTOR_CNT*4 and in
   * sync with LOSCFG_ARCH_HWI_VECTOR_ALIGN in target_config.h.
   */
  .vector (NOLOAD) :
  {
    . = ALIGN(0x800);
    KEEP(*(.vector))
    . = ALIGN(4);
  } >AXI_SRAM

  /* Uninitialized data section */
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >AXI_SRAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    PROVIDE ( __heap_start = . );
    . = . + _Min_Heap_Size;
    PROVIDE ( __heap_end = . );
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >AXI_SRAM

  

  /* NOTE: The original Nations linker script discarded libc.a/libm.a/libgcc.a here.
     For an OpenHarmony LiteOS-M build we MUST keep libgcc (AEABI helpers, soft-fp)
     and (usually) libc/libm, otherwise linking fails on undefined __aeabi_* / __gedf2
     etc. Removed the /DISCARD/ block on purpose. */

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

