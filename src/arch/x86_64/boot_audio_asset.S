.section .rodata
.balign 16
.global capyos_boot_wav_start
.global capyos_boot_wav_end
capyos_boot_wav_start:
.incbin "build/generated/sounds/boot.wav"
capyos_boot_wav_end:
.section .note.GNU-stack,"",@progbits
