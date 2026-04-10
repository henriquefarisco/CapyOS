#ifndef KERNEL_ELF_LOADER_H
#define KERNEL_ELF_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "memory/vmm.h"
#include "kernel/process.h"

#define ELF_MAGIC 0x464C457F

#define ET_EXEC 2
#define ET_DYN  3

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define EM_X86_64 62

struct elf64_header {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct elf64_phdr {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
};

struct elf_load_result {
  uint64_t entry_point;
  uint64_t brk;
  uint64_t phdr_vaddr;
  uint16_t phnum;
  int success;
};

int elf_validate(const uint8_t *data, size_t size);
int elf_load(struct vmm_address_space *as, const uint8_t *data, size_t size,
             struct elf_load_result *result);
int elf_load_from_file(struct process *proc, const char *path);

#endif /* KERNEL_ELF_LOADER_H */
