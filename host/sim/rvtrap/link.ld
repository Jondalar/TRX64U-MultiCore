SECTIONS {
  . = 0x0;   .text.vec     : { *(.text.vec) }
  . = 0x100; .text.handler : { *(.text.handler) }
  . = 0x400; .text         : { *(.text) }
  . = 0x900; .text.b       : { *(.text.b) }
  . = 0xC90; .text.alt     : { *(.text.alt) }
}
