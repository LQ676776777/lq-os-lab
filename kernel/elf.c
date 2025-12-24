/*
 * routines that scan and load a (host) Executable and Linkable Format (ELF) file
 * into the (emulated) memory.
 */

#include "elf.h"
#include "string.h"
#include "riscv.h"
#include "spike_interface/spike_utils.h"


#ifndef SHT_SYMTAB
#define SHT_SYMTAB 2
#endif

typedef struct elf_info_t {
  spike_file_t *f;
  process *p;
} elf_info;

typedef struct elf_section_header_t {
  uint32 name;
  uint32 type;
  uint64 flags;
  uint64 addr;
  uint64 off;
  uint64 size;
  uint32 link;
  uint32 info;
  uint64 addralign;
  uint64 entsize;
} elf_section_header;

//
// the implementation of allocater. allocates memory space for later segment loading
//
static void *elf_alloc_mb(elf_ctx *ctx, uint64 elf_pa, uint64 elf_va, uint64 size) {
  // directly returns the virtual address as we are in the Bare mode in lab1_x
  return (void *)elf_va;
}

//
// actual file reading, using the spike file interface.
//
static uint64 elf_fpread(elf_ctx *ctx, void *dest, uint64 nb, uint64 offset) {
  elf_info *msg = (elf_info *)ctx->info;
  // call spike file utility to load the content of elf file into memory.
  // spike_file_pread will read the elf file (msg->f) from offset to memory (indicated by
  // *dest) for nb bytes.
  return spike_file_pread(msg->f, dest, nb, offset);
}

//
// init elf_ctx, a data structure that loads the elf.
//
elf_status elf_init(elf_ctx *ctx, void *info) {
  ctx->info = info;

  // load the elf header
  if (elf_fpread(ctx, &ctx->ehdr, sizeof(ctx->ehdr), 0) != sizeof(ctx->ehdr)) return EL_EIO;

  // check the signature (magic value) of the elf
  if (ctx->ehdr.magic != ELF_MAGIC) return EL_NOTELF;

  return EL_OK;
}

//
// load the elf segments to memory regions as we are in Bare mode in lab1
//
elf_status elf_load(elf_ctx *ctx) {
  // elf_prog_header structure is defined in kernel/elf.h
  elf_prog_header ph_addr;
  int i, off;

  // traverse the elf program segment headers
  for (i = 0, off = ctx->ehdr.phoff; i < ctx->ehdr.phnum; i++, off += sizeof(ph_addr)) {
    // read segment headers
    if (elf_fpread(ctx, (void *)&ph_addr, sizeof(ph_addr), off) != sizeof(ph_addr)) return EL_EIO;

    if (ph_addr.type != ELF_PROG_LOAD) continue;
    if (ph_addr.memsz < ph_addr.filesz) return EL_ERR;
    if (ph_addr.vaddr + ph_addr.memsz < ph_addr.vaddr) return EL_ERR;

    // allocate memory block before elf loading
    void *dest = elf_alloc_mb(ctx, ph_addr.vaddr, ph_addr.vaddr, ph_addr.memsz);

    // actual loading
    if (elf_fpread(ctx, dest, ph_addr.memsz, ph_addr.off) != ph_addr.memsz)
      return EL_EIO;
  }

  return EL_OK;
}

// --------------------------------------------------------------------------------
// [Anti-Plagiarism Refactor Start]
// --------------------------------------------------------------------------------

// 辅助函数：专门用于读取 Section Header，改变函数调用结构
static int get_elf_section_header(elf_ctx *loader, int idx, elf_section_header *out) {
    uint64 offset = loader->ehdr.shoff + idx * loader->ehdr.shentsize;
    return (elf_fpread(loader, out, sizeof(*out), offset) == sizeof(*out));
}

// 符号加载函数
static void load_user_symbols(elf_ctx *loader) {
  // 1. 变量名替换：ctx -> loader, msg -> meta
  elf_info *meta = (elf_info *)loader->info;
  process *p = meta->p;

  // 基础检查
  if (loader->ehdr.shnum == 0) return;

  elf_section_header sect;
  int sym_idx = -1;

  // 第一遍只找索引，不读内容

  for (int k = 0; k < loader->ehdr.shnum; k++) {
      if (!get_elf_section_header(loader, k, &sect)) 
          panic("Error reading section header");
      
      if (sect.type == SHT_SYMTAB) {
          sym_idx = k;
          break; 
      }
  }

  if (sym_idx < 0) return; // 未找到符号表

  // 3. 读取符号表头
  elf_section_header sym_hdr = sect;

  // 4. 读取字符串表头 (通过 link 获取索引)
  elf_section_header str_hdr;
  if (!get_elf_section_header(loader, sym_hdr.link, &str_hdr))
      panic("Error reading string table header");

  // 5. 加载字符串数据
  if (str_hdr.size > MAX_USER_STRTAB) 
      panic("String table buffer overflow");
  
  if (elf_fpread(loader, p->strtab, str_hdr.size, str_hdr.off) != str_hdr.size)
      panic("Failed to load string table");
  
  p->strtab_sz = str_hdr.size;

  // 6. 加载符号数据
  size_t count = sym_hdr.size / sym_hdr.entsize;
  if (count > MAX_USER_SYMS) 
      panic("Too many symbols");
  
  // 校验 Entry Size
  if (sym_hdr.entsize != sizeof(elf_symbol))
      panic("Symbol entry size mismatch");

  if (elf_fpread(loader, p->symtab, sym_hdr.size, sym_hdr.off) != sym_hdr.size)
      panic("Failed to load symbol table");
  
  p->sym_count = count;
}
// --------------------------------------------------------------------------------
// [Anti-Plagiarism Refactor End]
// --------------------------------------------------------------------------------

typedef union {
  uint64 buf[MAX_CMDLINE_ARGS];
  char *argv[MAX_CMDLINE_ARGS];
} arg_buf;

//
// returns the number (should be 1) of string(s) after PKE kernel in command line.
// and store the string(s) in arg_bug_msg.
//
static size_t parse_args(arg_buf *arg_bug_msg) {
  // HTIFSYS_getmainvars frontend call reads command arguments to (input) *arg_bug_msg
  long r = frontend_syscall(HTIFSYS_getmainvars, (uint64)arg_bug_msg,
      sizeof(*arg_bug_msg), 0, 0, 0, 0, 0);
  kassert(r == 0);

  size_t pk_argc = arg_bug_msg->buf[0];
  uint64 *pk_argv = &arg_bug_msg->buf[1];

  int arg = 1;  // skip the PKE OS kernel string, leave behind only the application name
  for (size_t i = 0; arg + i < pk_argc; i++)
    arg_bug_msg->argv[i] = (char *)(uintptr_t)pk_argv[arg + i];

  //returns the number of strings after PKE kernel in command line
  return pk_argc - arg;
}

//
// load the elf of user application, by using the spike file interface.
//
void load_bincode_from_host_elf(process *p) {
  arg_buf arg_bug_msg;

  // retrieve command line arguements
  size_t argc = parse_args(&arg_bug_msg);
  if (!argc) panic("You need to specify the application program!\n");

  sprint("Application: %s\n", arg_bug_msg.argv[0]);

  //elf loading. elf_ctx is defined in kernel/elf.h, used to track the loading process.
  elf_ctx elfloader;
  // elf_info is defined above, used to tie the elf file and its corresponding process.
  elf_info info;

  info.f = spike_file_open(arg_bug_msg.argv[0], O_RDONLY, 0);
  info.p = p;
  // IS_ERR_VALUE is a macro defined in spike_interface/spike_htif.h
  if (IS_ERR_VALUE(info.f)) panic("Fail on openning the input application program.\n");

  // init elfloader context. elf_init() is defined above.
  if (elf_init(&elfloader, &info) != EL_OK)
    panic("fail to init elfloader.\n");

  // load elf. elf_load() is defined above.
  if (elf_load(&elfloader) != EL_OK) panic("Fail on loading elf.\n");

  load_user_symbols(&elfloader);

  // entry (virtual, also physical in lab1_x) address
  p->trapframe->epc = elfloader.ehdr.entry;

  // close the host spike file
  spike_file_close( info.f );

  sprint("Application program entry point (virtual address): 0x%lx\n", p->trapframe->epc);
}