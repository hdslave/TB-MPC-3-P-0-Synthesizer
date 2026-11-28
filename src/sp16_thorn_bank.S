.section .rodata
.balign 4
.global slink_sp16_thorn_bank
.hidden slink_sp16_thorn_bank
slink_sp16_thorn_bank:
.incbin "assets/sp16-thorn/panel.bank"
.balign 4
.global slink_sp16_controls
.hidden slink_sp16_controls
slink_sp16_controls:
.incbin "assets/sp16-thorn/controls.bank"
.section .note.GNU-stack,"",%progbits
