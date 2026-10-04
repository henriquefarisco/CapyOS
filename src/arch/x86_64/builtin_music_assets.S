.section .rodata
.balign 16
.global capyos_acoustic_start, capyos_acoustic_end
capyos_acoustic_start:
.incbin "build/generated/sounds/Capy Acoustic.ogg"
capyos_acoustic_end:
.global capyos_opera_start, capyos_opera_end
capyos_opera_start:
.incbin "build/generated/sounds/Capy Opera.ogg"
capyos_opera_end:
.global capyos_sound_start, capyos_sound_end
capyos_sound_start:
.incbin "build/generated/sounds/Capy Sound.ogg"
capyos_sound_end:
.section .note.GNU-stack,"",@progbits
