// The firmware gets these symbols from PlatformIO's embed_files; here the
// assembler pulls the same font file in.
    .section __DATA,__const
    .globl _binary_data_ui_font_vlw_start
    .globl _binary_data_ui_font_vlw_end
_binary_data_ui_font_vlw_start:
    .incbin "data/ui_font.vlw"
_binary_data_ui_font_vlw_end:
    .byte 0
