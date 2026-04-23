typedef EFI_STATUS (*kernel_read_fn_t)(void *ctx, UINT64 offset, VOID *buf,
                                       UINTN size);

struct kernel_buffer_reader {
  const UINT8 *base;
  UINT64 size;
};

struct kernel_block_reader {
  EFI_BLOCK_IO_PROTOCOL *bio;
  UINT64 base_lba;
  UINT32 block_size;
  UINT64 size;
};

struct kernel_file_reader {
  EFI_FILE_HANDLE file;
  UINT64 size;
};

struct kernel_load_plan {
  EFI_PHYSICAL_ADDRESS link_base;
  EFI_PHYSICAL_ADDRESS link_end;
  UINT64 entry;
  UINTN span;
  UINTN pages;
};

static void kernel_copy_bytes(VOID *dst, const VOID *src, UINTN len) {
  UINT8 *d = (UINT8 *)dst;
  const UINT8 *s = (const UINT8 *)src;
  for (UINTN i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static void kernel_zero_bytes(VOID *dst, UINTN len) {
  UINT8 *d = (UINT8 *)dst;
  for (UINTN i = 0; i < len; ++i) {
    d[i] = 0;
  }
}

static EFI_STATUS kernel_read_from_memory(void *ctx, UINT64 offset, VOID *buf,
                                          UINTN size) {
  struct kernel_buffer_reader *reader = (struct kernel_buffer_reader *)ctx;
  if (!reader || !reader->base || !buf) {
    return EFI_INVALID_PARAMETER;
  }
  if (offset > reader->size || (UINT64)size > (reader->size - offset)) {
    return EFI_LOAD_ERROR;
  }
  kernel_copy_bytes(buf, reader->base + (UINTN)offset, size);
  return EFI_SUCCESS;
}

static EFI_STATUS kernel_read_from_blocks(void *ctx, UINT64 offset, VOID *buf,
                                          UINTN size) {
  struct kernel_block_reader *reader = (struct kernel_block_reader *)ctx;
  UINT8 *dst = (UINT8 *)buf;

  if (!reader || !reader->bio || !reader->bio->Media || !buf) {
    return EFI_INVALID_PARAMETER;
  }
  if (reader->block_size == 0 ||
      reader->block_size > sizeof(g_kernel_block_scratch)) {
    return EFI_UNSUPPORTED;
  }
  if (offset > reader->size || (UINT64)size > (reader->size - offset)) {
    return EFI_LOAD_ERROR;
  }

  while (size > 0) {
    UINTN block_offset = (UINTN)(offset % reader->block_size);
    EFI_STATUS st = uefi_call_wrapper(
        reader->bio->ReadBlocks, 5, reader->bio, reader->bio->Media->MediaId,
        reader->base_lba + (offset / reader->block_size), reader->block_size,
        g_kernel_block_scratch);
    if (EFI_ERROR(st)) {
      return st;
    }

    UINTN chunk = reader->block_size - block_offset;
    if (chunk > size) {
      chunk = size;
    }
    kernel_copy_bytes(dst, g_kernel_block_scratch + block_offset, chunk);
    offset += chunk;
    dst += chunk;
    size -= chunk;
  }

  return EFI_SUCCESS;
}

static EFI_STATUS kernel_read_from_file(void *ctx, UINT64 offset, VOID *buf,
                                        UINTN size) {
  struct kernel_file_reader *reader = (struct kernel_file_reader *)ctx;
  UINTN request = size;
  EFI_STATUS st;

  if (!reader || !reader->file || !buf) {
    return EFI_INVALID_PARAMETER;
  }
  if (offset > reader->size || (UINT64)size > (reader->size - offset)) {
    return EFI_LOAD_ERROR;
  }

  st = uefi_call_wrapper(reader->file->SetPosition, 2, reader->file, offset);
  if (EFI_ERROR(st)) {
    return st;
  }
  st = uefi_call_wrapper(reader->file->Read, 3, reader->file, &request, buf);
  if (EFI_ERROR(st)) {
    return st;
  }
  return request == size ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

static EFI_STATUS kernel_plan_from_headers(const Elf64_Ehdr *eh,
                                           const Elf64_Phdr *phdrs,
                                           UINT64 kernel_size,
                                           struct kernel_load_plan *plan) {
  EFI_PHYSICAL_ADDRESS link_base = 0xFFFFFFFFFFFFFFFFULL;
  EFI_PHYSICAL_ADDRESS link_end = 0;

  if (!eh || !phdrs || !plan) {
    return EFI_INVALID_PARAMETER;
  }
  if (*(const UINT32 *)eh->e_ident != ELF_MAGIC || eh->e_machine != EM_X86_64) {
    Print(L"[UEFI] kernel ELF64 invÃ¡lido\r\n");
    return EFI_UNSUPPORTED;
  }
  if (eh->e_phnum == 0 || eh->e_phnum > KERNEL_MAX_PHDRS) {
    Print(L"[UEFI] ELF com numero de program headers invalido: %u\r\n",
          (UINT32)eh->e_phnum);
    return EFI_LOAD_ERROR;
  }
  if (eh->e_phentsize != sizeof(Elf64_Phdr)) {
    Print(L"[UEFI] ELF com tamanho de program header nao suportado: %u\r\n",
          (UINT32)eh->e_phentsize);
    return EFI_UNSUPPORTED;
  }
  if (eh->e_phoff > kernel_size ||
      (UINT64)eh->e_phnum > ((kernel_size - eh->e_phoff) / sizeof(Elf64_Phdr))) {
    Print(L"[UEFI] ELF com program headers fora do arquivo\r\n");
    return EFI_LOAD_ERROR;
  }

  for (UINT16 i = 0; i < eh->e_phnum; ++i) {
    const Elf64_Phdr *ph = &phdrs[i];
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
      continue;
    }
    if (ph->p_offset > kernel_size ||
        ph->p_filesz > (kernel_size - ph->p_offset) ||
        ph->p_filesz > ph->p_memsz) {
      Print(L"[UEFI] Segmento ELF invalido (seg %u)\r\n", (UINT32)i);
      return EFI_LOAD_ERROR;
    }
    EFI_PHYSICAL_ADDRESS seg_start = ph->p_paddr & ~0xFFFULL;
    EFI_PHYSICAL_ADDRESS seg_end =
        (ph->p_paddr + ph->p_memsz + 0xFFFULL) & ~0xFFFULL;
    if (seg_start < link_base) {
      link_base = seg_start;
    }
    if (seg_end > link_end) {
      link_end = seg_end;
    }
  }

  if (link_base == 0xFFFFFFFFFFFFFFFFULL || link_end <= link_base) {
    Print(L"[UEFI] ELF sem segmentos PT_LOAD validos\r\n");
    return EFI_LOAD_ERROR;
  }
  if (eh->e_entry < link_base || eh->e_entry >= link_end) {
    Print(L"[UEFI] Entry point fora da imagem do kernel\r\n");
    return EFI_LOAD_ERROR;
  }

  plan->link_base = link_base;
  plan->link_end = link_end;
  plan->entry = eh->e_entry;
  plan->span = (UINTN)(link_end - link_base);
  plan->pages = (plan->span + 0xFFFU) >> 12;
  return EFI_SUCCESS;
}

static EFI_STATUS kernel_reserve_fixed_window(EFI_SYSTEM_TABLE *st) {
  EFI_PHYSICAL_ADDRESS reserve_base = KERNEL_FIXED_RESERVE_BASE;
  UINTN reserve_pages = (UINTN)(KERNEL_FIXED_RESERVE_BYTES >> 12);
  EFI_STATUS stt;

  if (!st || !st->BootServices) {
    return EFI_INVALID_PARAMETER;
  }
  if (g_kernel_reserved_pages != 0) {
    return EFI_SUCCESS;
  }

  stt = uefi_call_wrapper(st->BootServices->AllocatePages, 4, AllocateAddress,
                          EfiLoaderCode, reserve_pages, &reserve_base);
  if (EFI_ERROR(stt)) {
    return stt;
  }

  g_kernel_reserved_base = reserve_base;
  g_kernel_reserved_pages = reserve_pages;
  return EFI_SUCCESS;
}

static void kernel_release_fixed_window(EFI_SYSTEM_TABLE *st) {
  if (!st || !st->BootServices || g_kernel_reserved_pages == 0) {
    return;
  }
  uefi_call_wrapper(st->BootServices->FreePages, 2, g_kernel_reserved_base,
                    g_kernel_reserved_pages);
  g_kernel_reserved_base = 0;
  g_kernel_reserved_pages = 0;
}

static EFI_STATUS kernel_acquire_load_pages(EFI_SYSTEM_TABLE *st,
                                            EFI_PHYSICAL_ADDRESS link_base,
                                            UINTN pages,
                                            EFI_PHYSICAL_ADDRESS *load_base) {
  EFI_STATUS stt;
  EFI_PHYSICAL_ADDRESS reloc_base = 0xFFFFFFFFULL;

  if (!st || !st->BootServices || !load_base || pages == 0) {
    return EFI_INVALID_PARAMETER;
  }

  *load_base = link_base;
  if (g_kernel_reserved_pages != 0 && g_kernel_reserved_base == link_base) {
    if (pages <= g_kernel_reserved_pages) {
      return EFI_SUCCESS;
    }
    Print(L"[UEFI] Reserva antecipada do kernel insuficiente: base=0x%lx pages=%lu need=%lu; tentando alocacao exata\r\n",
          g_kernel_reserved_base, (UINT64)g_kernel_reserved_pages,
          (UINT64)pages);
    kernel_release_fixed_window(st);
    *load_base = link_base;
  }

  stt = uefi_call_wrapper(st->BootServices->AllocatePages, 4, AllocateAddress,
                          EfiLoaderCode, pages, load_base);
  if (!EFI_ERROR(stt)) {
    return EFI_SUCCESS;
  }

  Print(L"[UEFI] AllocatePages(kernel@0x%lx) falhou: %r\r\n", link_base, stt);

  stt = uefi_call_wrapper(st->BootServices->AllocatePages, 4, AllocateMaxAddress,
                          EfiLoaderCode, pages, &reloc_base);
  if (EFI_ERROR(stt)) {
    Print(L"[UEFI] FATAL: fallback relocado do kernel falhou: %r\r\n", stt);
    return stt;
  }

  *load_base = reloc_base;
  Print(L"[UEFI] Aviso: kernel carregado fora do link-base em 0x%lx\r\n",
        reloc_base);
  return EFI_SUCCESS;
}

static void kernel_release_load_pages(EFI_SYSTEM_TABLE *st,
                                      EFI_PHYSICAL_ADDRESS load_base,
                                      UINTN pages) {
  if (!st || !st->BootServices || pages == 0) {
    return;
  }
  if (g_kernel_reserved_pages != 0 && g_kernel_reserved_base == load_base) {
    kernel_release_fixed_window(st);
    return;
  }
  uefi_call_wrapper(st->BootServices->FreePages, 2, load_base, pages);
}

static EFI_STATUS load_kernel_from_reader(EFI_SYSTEM_TABLE *st,
                                          kernel_read_fn_t reader, void *ctx,
                                          UINT64 kernel_size,
                                          EFI_PHYSICAL_ADDRESS *entry_out) {
  Elf64_Ehdr eh;
  Elf64_Phdr phdrs[KERNEL_MAX_PHDRS];
  struct kernel_load_plan plan;
  EFI_PHYSICAL_ADDRESS load_base;
  EFI_STATUS read_st;

  if (!st || !reader || !entry_out) {
    return EFI_INVALID_PARAMETER;
  }
  if (kernel_size < sizeof(Elf64_Ehdr)) {
    Print(L"[UEFI] kernel muito pequeno\r\n");
    return EFI_LOAD_ERROR;
  }

  read_st = reader(ctx, 0, &eh, sizeof(eh));
  if (EFI_ERROR(read_st)) {
    Print(L"[UEFI] Falha ao ler cabecalho ELF: %r\r\n", read_st);
    return read_st;
  }
  if (eh.e_phnum > KERNEL_MAX_PHDRS || eh.e_phentsize != sizeof(Elf64_Phdr)) {
    return kernel_plan_from_headers(&eh, phdrs, kernel_size, &plan);
  }

  read_st = reader(ctx, eh.e_phoff, phdrs,
                   (UINTN)eh.e_phnum * sizeof(Elf64_Phdr));
  if (EFI_ERROR(read_st)) {
    Print(L"[UEFI] Falha ao ler program headers do kernel: %r\r\n", read_st);
    return read_st;
  }

  read_st = kernel_plan_from_headers(&eh, phdrs, kernel_size, &plan);
  if (EFI_ERROR(read_st)) {
    return read_st;
  }

  load_base = plan.link_base;
  read_st =
      kernel_acquire_load_pages(st, plan.link_base, plan.pages, &load_base);
  if (EFI_ERROR(read_st)) {
    return read_st;
  }

  Print(L"[UEFI] Kernel plan: link=0x%lx span=0x%lx pages=%lu load=0x%lx\r\n",
        plan.link_base, (UINT64)plan.span, (UINT64)plan.pages, load_base);

  for (UINT16 i = 0; i < eh.e_phnum; ++i) {
    const Elf64_Phdr *ph = &phdrs[i];
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
      continue;
    }
    EFI_PHYSICAL_ADDRESS dst_pa = load_base + (ph->p_paddr - plan.link_base);
    Print(L"[UEFI] Copiando seg %u dst=0x%lx fsz=0x%lx msz=0x%lx\r\n",
          (UINT32)i, (UINT64)dst_pa, (UINT64)ph->p_filesz,
          (UINT64)ph->p_memsz);
    if (ph->p_filesz != 0) {
      read_st = reader(ctx, ph->p_offset, (VOID *)(UINTN)dst_pa,
                       (UINTN)ph->p_filesz);
      if (EFI_ERROR(read_st)) {
        Print(L"[UEFI] Falha ao carregar segmento %u do kernel: %r\r\n",
              (UINT32)i, read_st);
        kernel_release_load_pages(st, load_base, plan.pages);
        return read_st;
      }
    }
  }

  *entry_out = load_base + (plan.entry - plan.link_base);
  Print(L"[UEFI] Kernel carregado @ 0x%lx\r\n", (UINT64)*entry_out);
  return EFI_SUCCESS;
}

static EFI_STATUS load_kernel_from_buffer(EFI_SYSTEM_TABLE *st,
                                          VOID *kernel_buf, UINTN kernel_size,
                                          EFI_PHYSICAL_ADDRESS *entry_out) {
  if (kernel_size < sizeof(Elf64_Ehdr)) {
    Print(L"[UEFI] kernel muito pequeno\r\n");
    return EFI_LOAD_ERROR;
  }

  Elf64_Ehdr *eh = (Elf64_Ehdr *)kernel_buf;
  if (*(UINT32 *)eh->e_ident != ELF_MAGIC || eh->e_machine != EM_X86_64) {
    Print(L"[UEFI] kernel ELF64 invÃƒÂ¡lido\r\n");
    return EFI_UNSUPPORTED;
  }

  /* O kernel 64-bit é linkado com endereços absolutos (sem relocações
   * dinâmicas). Calcula o VMA base (link_base) e o tamanho total da imagem
   * a partir dos segmentos PT_LOAD, depois aloca exatamente nesse endereço. */
  EFI_PHYSICAL_ADDRESS link_base = 0xFFFFFFFFFFFFFFFFULL;
  EFI_PHYSICAL_ADDRESS link_end = 0;

  Elf64_Phdr *ph = (Elf64_Phdr *)((UINT8 *)kernel_buf + eh->e_phoff);
  for (UINT16 i = 0; i < eh->e_phnum; i++, ph++) {
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
      continue;
    EFI_PHYSICAL_ADDRESS seg_start = ph->p_paddr & ~0xFFFULL;
    EFI_PHYSICAL_ADDRESS seg_end =
        (ph->p_paddr + ph->p_memsz + 0xFFFULL) & ~0xFFFULL;
    if (seg_start < link_base)
      link_base = seg_start;
    if (seg_end > link_end)
      link_end = seg_end;
  }
  if (link_base == 0xFFFFFFFFFFFFFFFFULL || link_end <= link_base) {
    Print(L"[UEFI] ELF sem segmentos PT_LOAD vÃƒÂ¡lidos\r\n");
    return EFI_LOAD_ERROR;
  }

  UINTN span = (UINTN)(link_end - link_base);
  UINTN pages = (span + 0xFFF) >> 12;

  /* O kernel é linkado com endereços absolutos (sem relocações dinâmicas),
   * portanto deve ser carregado exatamente no VMA de link (link_base).
   * Usamos AllocateAddress para garantir isso; se o firmware já ocupou
   * essa região, não há fallback seguro pois as funções pointer internas
   * apontariam para endereços errados. */
  EFI_PHYSICAL_ADDRESS load_base = link_base;
  EFI_STATUS alloc_st =
      kernel_acquire_load_pages(st, link_base, pages, &load_base);
  if (EFI_ERROR(alloc_st)) {
    return alloc_st;
  }

  Print(L"[UEFI] Kernel plan: link=0x%lx span=0x%lx pages=%lu load=0x%lx\r\n",
        link_base, (UINT64)span, (UINT64)pages, load_base);
  /* IMPORTANTE: NAO zerar o span inteiro com kernel_zero_bytes().
   * O span pode atingir ~20MB (kheap estatico de 16MB no .bss) e algumas
   * implementacoes UEFI (VMware) reservam paginas internas dentro da regiao
   * dada por AllocatePages que, quando sobrescritas, derrubam o IDT/heap do
   * firmware e geram triple fault no proximo Print/copy. Em vez disso,
   * deixamos o entry64.S zerar a .bss no primeiro stack do kernel. */

  // Copia os segmentos para o novo base; a .bss fica para o entry64.S.
  ph = (Elf64_Phdr *)((UINT8 *)kernel_buf + eh->e_phoff);
  for (UINT16 i = 0; i < eh->e_phnum; i++, ph++) {
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
      continue;
    if (ph->p_offset + ph->p_filesz > kernel_size) {
      Print(L"[UEFI] Segmento fora do buffer (seg %d)\r\n", i);
      kernel_release_load_pages(st, load_base, pages);
      return EFI_LOAD_ERROR;
    }
    EFI_PHYSICAL_ADDRESS dst_pa = load_base + (ph->p_paddr - link_base);
    Print(L"[UEFI] Copiando seg %u src=0x%lx dst=0x%lx fsz=0x%lx msz=0x%lx\r\n",
          (UINT32)i, (UINT64)((UINTN)kernel_buf + ph->p_offset), dst_pa,
          (UINT64)ph->p_filesz, (UINT64)ph->p_memsz);
    if (ph->p_filesz > 0) {
      kernel_copy_bytes((VOID *)(UINTN)dst_pa,
                        (UINT8 *)kernel_buf + ph->p_offset,
                        (UINTN)ph->p_filesz);
    }
    /* NAO zerar o BSS aqui: o entry64.S faz a zeragem defensiva antes de
     * chamar o C, fora da janela critica do firmware. */
  }

  *entry_out = (EFI_PHYSICAL_ADDRESS)(load_base + (eh->e_entry - link_base));
  Print(L"[UEFI] Kernel carregado @ 0x%lx\r\n", (UINT64)*entry_out);
  return EFI_SUCCESS;
}

