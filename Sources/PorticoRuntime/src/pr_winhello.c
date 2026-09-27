/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_winhello.h"

#include <stdlib.h>
#include <string.h>

/* PE32 mínimos, layout FIXO e SEM sobreposição (validado por testes):
 *
 * RVA 0x1000  descritores de import (20 B cada; terminador NULO incluso)
 * RVA 0x1030  ILT/OFT (thunks por nome)
 * RVA 0x1040  nomes de DLL (ASCII)
 * RVA 0x1060  hintnames (u16 hint + nome + NUL)
 * RVA 0x1090  IAT (copia do ILT; preenchido pelo loader com os thunks reais)
 * RVA 0x2000  .text (entry point 0x00402000)
 * image base 0x00400000, size_of_image 0x3000, file size 0x600
 *
 * hello: IAT 0x1090=GetTickCount64, 0x1094=ExitProcess
 * gdi:   IAT 0x10E0=ExitProcess, 0x10E4=CreateCompatibleDC,
 *        0x10E8=CreateSolidBrush, 0x10EC=SelectObject, 0x10F0=PatBlt
 */

static void w16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }
static void w32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }

static size_t pe_skeleton(uint8_t* buf, size_t cap) {
    const size_t FILE_SIZE = 0x600;
    if (cap < FILE_SIZE) return 0;
    memset(buf, 0, FILE_SIZE);

    /* DOS */
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);

    /* PE + COFF: 2 seções, PE32, executável */
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x014C);   /* i386 */
    w16(buf + pe + 6, 2);        /* 2 seções */
    w16(buf + pe + 20, 224);     /* optional header PE32 */
    w16(buf + pe + 22, 0x0102);  /* EXECUTABLE | 32BIT */

    /* Optional PE32 */
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x10B);
    w32(buf + opt + 16, 0x2000);      /* entry */
    w32(buf + opt + 20, 0x2000);      /* base of code */
    w32(buf + opt + 24, 0x1000);      /* base of data */
    w32(buf + opt + 28, 0x00400000);  /* image base */
    w32(buf + opt + 32, 0x1000);
    w32(buf + opt + 36, 0x200);
    w32(buf + opt + 56, 0x3000);      /* size of image */
    w32(buf + opt + 60, 0x200);       /* size of headers */
    w16(buf + opt + 68, 3);           /* console */
    w32(buf + opt + 92, 16);
    w32(buf + opt + 96 + 0, 0);       /* sem exports */
    w32(buf + opt + 96 + 8, 0x1000);  /* imports rva */
    w32(buf + opt + 96 + 12, 0x200);

    /* Seções: .rdata (R) e .text (RX) */
    uint32_t sec = opt + 224;
    memcpy(buf + sec, ".rdata\0\0", 8);
    w32(buf + sec + 8, 0x200);
    w32(buf + sec + 12, 0x1000);
    w32(buf + sec + 16, 0x200);
    w32(buf + sec + 20, 0x200);
    w32(buf + sec + 36, 0x40000040);  /* INIT_DATA | READ */
    memcpy(buf + sec + 40, ".text\0\0\0", 8);
    w32(buf + sec + 48, 0x200);
    w32(buf + sec + 52, 0x2000);
    w32(buf + sec + 56, 0x200);
    w32(buf + sec + 60, 0x400);
    w32(buf + sec + 76, 0x60000020);  /* CODE | EXECUTE | READ */
    return FILE_SIZE;
}

/* offset no arquivo = RVA - 0xE00 (headers 0x200 paginados em 0x1000) */
#define RVA2OFF(rva) ((rva) - 0xE00)

/* variante 0: hello (kernel32!GetTickCount64 + kernel32!ExitProcess(42)) */
static size_t build_hello(uint8_t* buf, size_t cap) {
    size_t n = pe_skeleton(buf, cap);
    if (!n) return 0;

    /* descritor único (0x1000) + nulo (0x1014) */
    w32(buf + RVA2OFF(0x1000) + 0x00, 0x1030);  /* OFT */
    w32(buf + RVA2OFF(0x1000) + 0x0C, 0x1040);  /* Name */
    w32(buf + RVA2OFF(0x1000) + 0x10, 0x1090);  /* FirstThunk (IAT) */

    /* ILT (0x1030) */
    w32(buf + RVA2OFF(0x1030), 0x1060);
    w32(buf + RVA2OFF(0x1034), 0x1072);
    /* terminador implícito (zeros) em 0x1038 */

    memcpy(buf + RVA2OFF(0x1040), "KERNEL32.dll", 13);

    /* hintnames */
    w16(buf + RVA2OFF(0x1060), 0);
    memcpy(buf + RVA2OFF(0x1060) + 2, "GetTickCount64", 15);
    w16(buf + RVA2OFF(0x1072), 0);
    memcpy(buf + RVA2OFF(0x1072) + 2, "ExitProcess", 12);

    /* IAT (0x1090) — copia do ILT */
    w32(buf + RVA2OFF(0x1090), 0x1060);
    w32(buf + RVA2OFF(0x1094), 0x1072);

    /* código (RVA 0x2000) */
    uint8_t* c = buf + 0x400;
    size_t o = 0;
    c[o++] = 0xFF; c[o++] = 0x15;                       /* call [imm32] */
    w32(c + o, 0x00401090); o += 4;                     /* GetTickCount64 */
    c[o++] = 0x68; w32(c + o, 42); o += 4;              /* push 42 */
    c[o++] = 0xFF; c[o++] = 0x15;                       /* call [imm32] */
    w32(c + o, 0x00401094); o += 4;                     /* ExitProcess */
    c[o++] = 0xF4;                                      /* hlt (segurança) */
    return n;
}

/* variante 1: gdi (retângulo vermelho via PatBlt) */
static size_t build_gdi(uint8_t* buf, size_t cap) {
    size_t n = pe_skeleton(buf, cap);
    if (!n) return 0;

    /* descritores: k32 (0x1000), gdi32 (0x1014), nulo (0x1028) */
    w32(buf + RVA2OFF(0x1000) + 0x00, 0x1040);  /* k32 OFT */
    w32(buf + RVA2OFF(0x1000) + 0x0C, 0x1060);  /* k32 Name */
    w32(buf + RVA2OFF(0x1000) + 0x10, 0x10E0);  /* k32 IAT */
    w32(buf + RVA2OFF(0x1014) + 0x00, 0x1048);  /* gdi OFT */
    w32(buf + RVA2OFF(0x1014) + 0x0C, 0x106E);  /* gdi Name */
    w32(buf + RVA2OFF(0x1014) + 0x10, 0x10E4);  /* gdi IAT */

    /* ILT k32 (0x1040): ExitProcess */
    w32(buf + RVA2OFF(0x1040), 0x1080);
    /* ILT gdi (0x1048): 4 funções na ordem usada pelo código */
    w32(buf + RVA2OFF(0x1048), 0x1090);  /* CreateCompatibleDC */
    w32(buf + RVA2OFF(0x104C), 0x10A4);  /* CreateSolidBrush   */
    w32(buf + RVA2OFF(0x1050), 0x10B8);  /* SelectObject       */
    w32(buf + RVA2OFF(0x1054), 0x10CC);  /* PatBlt             */

    memcpy(buf + RVA2OFF(0x1060), "KERNEL32.dll", 13);
    memcpy(buf + RVA2OFF(0x106E), "gdi32.dll", 10);

    /* hintnames */
    w16(buf + RVA2OFF(0x1080), 0);
    memcpy(buf + RVA2OFF(0x1080) + 2, "ExitProcess", 12);
    w16(buf + RVA2OFF(0x1090), 0);
    memcpy(buf + RVA2OFF(0x1090) + 2, "CreateCompatibleDC", 18);
    w16(buf + RVA2OFF(0x10A4), 0);
    memcpy(buf + RVA2OFF(0x10A4) + 2, "CreateSolidBrush", 17);
    w16(buf + RVA2OFF(0x10B8), 0);
    memcpy(buf + RVA2OFF(0x10B8) + 2, "SelectObject", 13);
    w16(buf + RVA2OFF(0x10CC), 0);
    memcpy(buf + RVA2OFF(0x10CC) + 2, "PatBlt", 7);

    /* IAT */
    w32(buf + RVA2OFF(0x10E0), 0x1080);
    w32(buf + RVA2OFF(0x10E4), 0x1090);
    w32(buf + RVA2OFF(0x10E8), 0x10A4);
    w32(buf + RVA2OFF(0x10EC), 0x10B8);
    w32(buf + RVA2OFF(0x10F0), 0x10CC);

    /* código */
    uint8_t* c = buf + 0x400;
    size_t o = 0;
    c[o++] = 0x6A; c[o++] = 0x00;                       /* push 0 */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x004010E4); o += 4; /* CreateCompatibleDC */
    c[o++] = 0x89; c[o++] = 0xC3;                       /* mov ebx, eax */
    c[o++] = 0x68; w32(c + o, 0x000000FF); o += 4;      /* push COLORREF vermelho */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x004010E8); o += 4; /* CreateSolidBrush */
    c[o++] = 0x50;                                      /* push eax */
    c[o++] = 0x53;                                      /* push ebx */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x004010EC); o += 4; /* SelectObject */
    c[o++] = 0x68; w32(c + o, 0x00F00021); o += 4;      /* push PATCOPY */
    c[o++] = 0x6A; c[o++] = 48;                         /* push 48 (h) */
    c[o++] = 0x6A; c[o++] = 64;                         /* push 64 (w) */
    c[o++] = 0x6A; c[o++] = 8;                          /* push 8 (y) */
    c[o++] = 0x6A; c[o++] = 8;                          /* push 8 (x) */
    c[o++] = 0x53;                                      /* push ebx (hdc) */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x004010F0); o += 4; /* PatBlt */
    c[o++] = 0x6A; c[o++] = 0x00;                       /* push 0 */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x004010E0); o += 4; /* ExitProcess */
    c[o++] = 0xF4;
    return n;
}

/* ============================================================
 * Variantes de compatibilidade Windows (Fase: PE32+ / relocations / ordinal)
 * ============================================================ */

/* PE32 genérico com n seções; alinhamento 0x1000/0x200; headers 0x200. */
static void pe32_head(uint8_t* buf, uint16_t nsec, uint32_t entry,
                      uint32_t image_base, uint32_t size_of_image,
                      uint32_t imp_rva, uint32_t reloc_rva, uint32_t reloc_sz) {
    memset(buf, 0, 0x800);
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x014C);
    w16(buf + pe + 6, nsec);
    w16(buf + pe + 20, 224);
    w16(buf + pe + 22, 0x0102);
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x10B);
    w32(buf + opt + 16, entry);
    w32(buf + opt + 20, 0x2000);
    w32(buf + opt + 24, 0x1000);
    w32(buf + opt + 28, image_base);
    w32(buf + opt + 32, 0x1000);   /* section alignment */
    w32(buf + opt + 36, 0x200);    /* file alignment */
    w32(buf + opt + 56, size_of_image);
    w32(buf + opt + 60, 0x200);
    w16(buf + opt + 68, 3);
    w32(buf + opt + 92, 16);
    w32(buf + opt + 96 + 8, imp_rva);
    w32(buf + opt + 96 + 12, imp_rva ? 0x200 : 0);
    w32(buf + opt + 96 + 40, reloc_rva);
    w32(buf + opt + 96 + 44, reloc_sz);
}

static void pe_sec_at(uint8_t* buf, uint32_t opt_size, int i,
                      const char* name, uint32_t vsz,
                      uint32_t va, uint32_t rsz, uint32_t raw, uint32_t ch) {
    uint32_t sec = 0x80 + 24 + opt_size + (uint32_t)i * 40;
    memset(buf + sec, 0, 40);
    memcpy(buf + sec, name, strlen(name));
    w32(buf + sec + 8, vsz);
    w32(buf + sec + 12, va);
    w32(buf + sec + 16, rsz);
    w32(buf + sec + 20, raw);
    w32(buf + sec + 36, ch);
}

static void pe32_sec(uint8_t* buf, int i, const char* name, uint32_t vsz,
                     uint32_t va, uint32_t rsz, uint32_t raw, uint32_t ch) {
    uint32_t sec = 0x80 + 24 + 224 + (uint32_t)i * 40;
    memset(buf + sec, 0, 40);
    memcpy(buf + sec, name, strlen(name));
    w32(buf + sec + 8, vsz);
    w32(buf + sec + 12, va);
    w32(buf + sec + 16, rsz);
    w32(buf + sec + 20, raw);
    w32(buf + sec + 36, ch);
}

/* variant 2: PE32+ x64 com imports (carga completa; execução recusada). */
static size_t build_pe32plus_imports(uint8_t* buf, size_t cap) {
    if (cap < 0x600) return 0;
    memset(buf, 0, 0x800);
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x8664);       /* amd64 */
    w16(buf + pe + 6, 2);
    w16(buf + pe + 20, 240);         /* optional PE32+ */
    w16(buf + pe + 22, 0x0102);
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x20B);           /* PE32+ */
    w32(buf + opt + 16, 0x2000);     /* entry */
    w32(buf + opt + 20, 0x2000);
    w32(buf + opt + 24, 0x00400000); /* image base 64-bit (baixo: cabe no VM) */
    w32(buf + opt + 28, 0);
    w32(buf + opt + 32, 0x1000);
    w32(buf + opt + 36, 0x200);
    w32(buf + opt + 56, 0x3000);
    w32(buf + opt + 60, 0x200);
    w16(buf + opt + 68, 3);
    w32(buf + opt + 108, 16);        /* NumberOfRvaAndSizes */
    w32(buf + opt + 112 + 8, 0x1000);  /* imports */
    w32(buf + opt + 112 + 12, 0x200);
    /* seções: .rdata (R) e .text (RX) */
    pe_sec_at(buf, 240, 0, ".rdata", 0x200, 0x1000, 0x200, 0x200, 0x40000040);
    pe_sec_at(buf, 240, 1, ".text", 0x200, 0x2000, 0x200, 0x400, 0x60000020);
    /* imports (RVA 0x1000): k32 GetTickCount64 + ExitProcess, thunks 8 bytes */
    uint8_t* imp = buf + 0x200;
    w32(imp + 0x00, 0x1030);          /* OFT */
    w32(imp + 0x0C, 0x1040);          /* Name */
    w32(imp + 0x10, 0x1080);          /* FirstThunk (IAT) */
    w32(imp + 0x30, 0x1060);          /* ILT u64 lo */
    w32(imp + 0x38, 0x1072);
    memcpy(imp + 0x40, "KERNEL32.dll", 13);
    w16(imp + 0x60, 0); memcpy(imp + 0x62, "GetTickCount64", 15);
    w16(imp + 0x72, 0); memcpy(imp + 0x74, "ExitProcess", 12);
    w32(imp + 0x80, 0x1060);          /* IAT u64 */
    w32(imp + 0x88, 0x1072);
    /* código x64 (nunca executado neste build): xor rax, rax; ret */
    uint8_t* c = buf + 0x400;
    c[0] = 0x48; c[1] = 0x31; c[2] = 0xC0; c[3] = 0xC3;
    return 0x600;
}

/* variant 3: PE32 com import POR ORDINAL (ws2_32!#9 htons, ordinal público
 * do winsock.def) + kernel32!ExitProcess por nome. Executa: exit = 0x0102. */
static size_t build_ordinal(uint8_t* buf, size_t cap) {
    if (cap < 0x600) return 0;
    pe32_head(buf, 2, 0x2000, 0x00400000, 0x3000, 0x1000, 0, 0);
    pe32_sec(buf, 0, ".rdata", 0x200, 0x1000, 0x200, 0x200, 0x40000040);
    pe32_sec(buf, 1, ".text", 0x200, 0x2000, 0x200, 0x400, 0x60000020);
    uint8_t* imp = buf + 0x200;
    /* desc ws2_32 (0x1000) e kernel32 (0x1014) + nulo (0x1028) */
    w32(imp + 0x00, 0x1030);          /* ws2_32 OFT */
    w32(imp + 0x0C, 0x1050);
    w32(imp + 0x10, 0x1080);          /* ws2_32 IAT */
    w32(imp + 0x14, 0x1038);          /* k32 OFT */
    w32(imp + 0x20, 0x1060);
    w32(imp + 0x24, 0x1088);          /* k32 IAT */
    w32(imp + 0x30, 0x80000009);      /* ILT ws2_32: ORDINAL 9 (htons) */
    w32(imp + 0x38, 0x1070);          /* ILT k32: ExitProcess por nome */
    memcpy(imp + 0x50, "ws2_32.dll", 11);
    memcpy(imp + 0x60, "KERNEL32.dll", 13);
    w16(imp + 0x70, 0); memcpy(imp + 0x72, "ExitProcess", 12);
    w32(imp + 0x80, 0x80000009);      /* IAT */
    w32(imp + 0x88, 0x1070);
    /* código: ExitProcess(htons(0x0201)) = ExitProcess(0x0102) */
    uint8_t* c = buf + 0x400;
    size_t o = 0;
    c[o++] = 0x68; w32(c + o, 0x0201); o += 4;          /* push 0x0201 */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x00401080); o += 4; /* htons */
    c[o++] = 0x50;                                       /* push eax */
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x00401088); o += 4; /* ExitProcess */
    c[o++] = 0xF4;
    return 0x600;
}

/* variant 4: PE32 COM RELOCATIONS (base preferida 0x02000000 fora do espaço
 * do processo → força relocação). HIGHLOW no disp do call [abs] e num slot
 * de dados. Executa: ExitProcess(42) pelo caminho realocalizado. */
static size_t build_reloc32(uint8_t* buf, size_t cap) {
    if (cap < 0x800) return 0;
    pe32_head(buf, 3, 0x2000, 0x02000000, 0x4000, 0x1000, 0x3000, 24);
    pe32_sec(buf, 0, ".rdata", 0x200, 0x1000, 0x200, 0x200, 0x40000040);
    pe32_sec(buf, 1, ".text", 0x200, 0x2000, 0x200, 0x400, 0x60000020);
    pe32_sec(buf, 2, ".reloc", 0x18, 0x3000, 0x200, 0x600, 0x42000040);
    uint8_t* imp = buf + 0x200;
    w32(imp + 0x00, 0x1030);
    w32(imp + 0x0C, 0x1040);
    w32(imp + 0x10, 0x1060);
    w32(imp + 0x30, 0x1070);
    memcpy(imp + 0x40, "KERNEL32.dll", 13);
    w16(imp + 0x70, 0); memcpy(imp + 0x72, "ExitProcess", 12);
    w32(imp + 0x60, 0x1070);
    /* slot de dados em 0x10B0: ponteiro preferido 0x02001234 */
    w32(imp + 0xB0, 0x02001234);
    /* código: push 42; call [0x02001060]; hlt */
    uint8_t* c = buf + 0x400;
    size_t o = 0;
    c[o++] = 0x68; w32(c + o, 42); o += 4;
    c[o++] = 0xFF; c[o++] = 0x15; w32(c + o, 0x02001060); o += 4;
    c[o++] = 0xF4;
    /* .reloc (RVA 0x3000, file 0x600): bloco 0x1000 (slot 0x0B0) +
     * bloco 0x2000 (disp do call em 0x007) */
    uint8_t* r = buf + 0x600;
    w32(r + 0x00, 0x1000); w32(r + 0x04, 12);
    w16(r + 0x08, 0x30B0); w16(r + 0x0A, 0x0000);
    w32(r + 0x0C, 0x2000); w32(r + 0x10, 12);
    w16(r + 0x14, 0x3007); w16(r + 0x16, 0x0000);
    return 0x800;
}

/* variant 5: PE32+ DIR64 (base preferida 0x140000000 → relocaliza no VM).
 * Loader-only: o step recusa x86-64 com diagnóstico de arquitetura. */
static size_t build_reloc64(uint8_t* buf, size_t cap) {
    if (cap < 0x600) return 0;
    memset(buf, 0, 0x800);
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x8664);
    w16(buf + pe + 6, 2);
    w16(buf + pe + 20, 240);
    w16(buf + pe + 22, 0x0102);
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x20B);
    w32(buf + opt + 16, 0x1000);       /* entry */
    w32(buf + opt + 20, 0x1000);
    w32(buf + opt + 24, 0x40000000);   /* image base lo: 0x140000000 */
    w32(buf + opt + 28, 0x00000001);   /* image base hi */
    w32(buf + opt + 32, 0x1000);
    w32(buf + opt + 36, 0x200);
    w32(buf + opt + 56, 0x3000);
    w32(buf + opt + 60, 0x200);
    w16(buf + opt + 68, 3);
    w32(buf + opt + 108, 16);
    w32(buf + opt + 112 + 40, 0x2000); /* Basereloc */
    w32(buf + opt + 112 + 44, 12);
    pe_sec_at(buf, 240, 0, ".rdata", 0x200, 0x1000, 0x200, 0x200, 0x40000040);
    pe_sec_at(buf, 240, 1, ".reloc", 0x0C, 0x2000, 0x200, 0x400, 0x42000040);
    /* slot DIR64 em 0x10B0: 0x0000000140001234 */
    uint8_t* imp = buf + 0x200;
    w32(imp + 0xB0, 0x40001234);
    w32(imp + 0xB4, 0x00000001);
    /* Basereloc: bloco page 0x1000, entrada tipo 10 offset 0x0B0 */
    uint8_t* r = buf + 0x400;
    w32(r + 0x00, 0x1000); w32(r + 0x04, 12);
    w16(r + 0x08, 0xA0B0); w16(r + 0x0A, 0x0000);
    return 0x600;
}

/* variant 6: PE VISUAL x64 (marco visual) — fundo branco + retângulo vermelho
 * + retângulo azul num bitmap GDI, BitBlt (SRCCOPY) p/ a superfície do
 * processo e ExitProcess(0). Código straight-line do subconjunto x64
 * (push/pop, movs, xor, sub rsp, mov [rsp+disp], call [rip+disp32], hlt).
 * Cores COLORREF 0x00BBGGRR → superfície XRGB8888 0x00RRGGBB:
 * branco 0x00FFFFFF, vermelho 0x000000FF, azul 0x00FF0000. */
static size_t build_visual_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    memset(buf, 0, 0xA00);
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x8664);       /* amd64 */
    w16(buf + pe + 6, 2);            /* seções */
    w16(buf + pe + 20, 240);         /* optional PE32+ */
    w16(buf + pe + 22, 0x0102);
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x20B);           /* PE32+ */
    w32(buf + opt + 16, 0x2000);     /* entry */
    w32(buf + opt + 20, 0x2000);
    w32(buf + opt + 24, 0x00400000); /* image base (baixo: cabe no VM) */
    w32(buf + opt + 28, 0);
    w32(buf + opt + 32, 0x1000);
    w32(buf + opt + 36, 0x200);
    w32(buf + opt + 56, 0x3000);     /* size of image */
    w32(buf + opt + 60, 0x200);
    w16(buf + opt + 68, 3);          /* console */
    w32(buf + opt + 108, 16);
    w32(buf + opt + 112 + 8, 0x1000);   /* import dir */
    w32(buf + opt + 112 + 12, 0x3C);
    w32(buf + opt + 112 + 96, 0x1200);  /* IAT dir */
    w32(buf + opt + 112 + 100, 0x48);
    pe_sec_at(buf, 240, 0, ".rdata", 0x400, 0x1000, 0x400, 0x200, 0x40000040);
    pe_sec_at(buf, 240, 1, ".text", 0x400, 0x2000, 0x400, 0x600, 0x60000020);

    /* ---- imports (RVA 0x1000; imp+X = RVA 0x1000+X) ---- */
    uint8_t* imp = buf + 0x200;
    w32(imp + 0x000, 0x1040);        /* desc k32: OFT */
    w32(imp + 0x00C, 0x10C8);        /* Name */
    w32(imp + 0x010, 0x1200);        /* FirstThunk (IAT) */
    w32(imp + 0x014, 0x1050);        /* desc gdi: OFT */
    w32(imp + 0x020, 0x10D8);
    w32(imp + 0x024, 0x1210);
    w32(imp + 0x040, 0x10F0);        /* ILT k32: ExitProcess */
    w32(imp + 0x050, 0x1100);        /* ILT gdi: 6 funções */
    w32(imp + 0x058, 0x1118);
    w32(imp + 0x060, 0x1134);
    w32(imp + 0x068, 0x1148);
    w32(imp + 0x070, 0x115C);
    w32(imp + 0x078, 0x1168);
    memcpy(imp + 0x0C8, "KERNEL32.dll", 13);
    memcpy(imp + 0x0D8, "gdi32.dll", 10);
    w16(imp + 0x0F0, 0); memcpy(imp + 0x0F2, "ExitProcess", 12);
    w16(imp + 0x100, 0); memcpy(imp + 0x102, "CreateCompatibleDC", 19);
    w16(imp + 0x118, 0); memcpy(imp + 0x11A, "CreateCompatibleBitmap", 23);
    w16(imp + 0x134, 0); memcpy(imp + 0x136, "SelectObject", 13);
    w16(imp + 0x148, 0); memcpy(imp + 0x14A, "CreateSolidBrush", 17);
    w16(imp + 0x15C, 0); memcpy(imp + 0x15E, "PatBlt", 7);
    w16(imp + 0x168, 0); memcpy(imp + 0x16A, "BitBlt", 7);
    w32(imp + 0x200, 0x10F0);        /* IAT k32 (u64; hi zero) */
    w32(imp + 0x210, 0x1100);        /* IAT gdi (u64 por entrada) */
    w32(imp + 0x218, 0x1118);
    w32(imp + 0x220, 0x1134);
    w32(imp + 0x228, 0x1148);
    w32(imp + 0x230, 0x115C);
    w32(imp + 0x238, 0x1168);

    /* ---- código x64 straight-line (RVA 0x2000) ---- */
    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define E32(v) do { uint32_t _i = (uint32_t)(v); memcpy(c + o, &_i, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define MOV_M32(disp8, v) do { EMIT(0xC7, 0x44, 0x24, (uint8_t)(disp8)); E32(v); } while (0)
#define PAINT(colorref, x, y, w, h) do { \
        EMIT(0xB9); E32(colorref);            /* mov ecx, COLORREF */ \
        CALL_IAT(0x1228);                     /* CreateSolidBrush */ \
        EMIT(0x48, 0x89, 0xC2);               /* mov rdx, rax */ \
        EMIT(0x4C, 0x89, 0xE9);               /* mov rcx, r13 */ \
        CALL_IAT(0x1220);                     /* SelectObject(hdcMem, brush) */ \
        EMIT(0x4C, 0x89, 0xE9);               /* mov rcx, r13 */ \
        EMIT(0xBA); E32(x);                   /* mov edx, x */ \
        EMIT(0x41, 0xB8); E32(y);             /* mov r8d, y */ \
        EMIT(0x41, 0xB9); E32(w);             /* mov r9d, w */ \
        MOV_M32(0x20, (h));                   /* [rsp+0x20] = h */ \
        MOV_M32(0x28, 0x00F00021u);           /* [rsp+0x28] = PATCOPY */ \
        CALL_IAT(0x1230);                     /* PatBlt */ \
    } while (0)

    EMIT(0x53);                        /* push rbx */
    EMIT(0x41, 0x54);                  /* push r12 */
    EMIT(0x41, 0x55);                  /* push r13 */
    EMIT(0x48, 0x83, 0xEC, 0x50);      /* sub rsp, 0x50 (shadow+args) */

    EMIT(0x31, 0xC9);                  /* xor ecx, ecx */
    CALL_IAT(0x1210);                  /* hdcScreen = CreateCompatibleDC(0) */
    EMIT(0x49, 0x89, 0xC4);            /* mov r12, rax */
    EMIT(0x31, 0xC9);
    CALL_IAT(0x1210);                  /* hdcMem = CreateCompatibleDC(0) */
    EMIT(0x49, 0x89, 0xC5);            /* mov r13, rax */

    EMIT(0x4C, 0x89, 0xE9);            /* mov rcx, r13 */
    EMIT(0xBA); E32(320);              /* mov edx, 320 */
    EMIT(0x41, 0xB8); E32(240);        /* mov r8d, 240 */
    CALL_IAT(0x1218);                  /* hbm = CreateCompatibleBitmap(...) */
    EMIT(0x48, 0x89, 0xC3);            /* mov rbx, rax */
    EMIT(0x4C, 0x89, 0xE9);            /* mov rcx, r13 */
    EMIT(0x48, 0x89, 0xDA);            /* mov rdx, rbx */
    CALL_IAT(0x1220);                  /* SelectObject(hdcMem, hbm) */

    PAINT(0x00FFFFFFu, 0, 0, 320, 240);     /* fundo branco */
    PAINT(0x000000FFu, 40, 40, 120, 80);    /* retângulo vermelho */
    PAINT(0x00FF0000u, 160, 120, 100, 60);  /* retângulo azul */

    EMIT(0x4C, 0x89, 0xE1);            /* mov rcx, r12 (hdcDst) */
    EMIT(0x31, 0xD2);                  /* xor edx, edx (xDst) */
    EMIT(0x45, 0x31, 0xC0);            /* xor r8d, r8d (yDst) */
    EMIT(0x41, 0xB9); E32(320);        /* mov r9d, 320 */
    MOV_M32(0x20, 240);                /* h */
    EMIT(0x4C, 0x89, 0x6C, 0x24, 0x28);/* mov [rsp+0x28], r13 (hdcSrc) */
    MOV_M32(0x30, 0);                  /* xSrc */
    MOV_M32(0x38, 0);                  /* ySrc */
    MOV_M32(0x40, 0x00CC0020u);        /* SRCCOPY */
    CALL_IAT(0x1238);                  /* BitBlt */

    EMIT(0x31, 0xC9);                  /* xor ecx, ecx */
    CALL_IAT(0x1200);                  /* ExitProcess(0) */
    EMIT(0xF4);                        /* hlt (segurança pós-exit) */

#undef PAINT
#undef MOV_M32
#undef CALL_IAT
#undef E32
#undef EMIT
    (void)o;
    return 0xA00;
}


/* ============================================================
 * Variantes 7-9 (FASE 5): PEs de teste x64 para o caminho completo
 * memória / GDI+frame / APIs. Mesma malha PE32+ do visual (v6).
 * ============================================================ */

typedef struct { const char* name; const char* fns[12]; size_t nfns; } imp_dll;

/* Monta imports em imp (offsets locais = RVA - rdata_rva).
 * iat_out[i] = RVA do slot IAT (u64) da i-ésima função em ordem.
 * Retorna offset livre após os nomes; dir_rva/dir_size = diretório import. */
static size_t imp_build(uint8_t* imp, uint32_t rdata_rva,
                        const imp_dll* dlls, size_t ndlls,
                        uint32_t* iat_out, size_t iat_cap,
                        uint32_t* dir_rva, uint32_t* dir_size) {
    size_t off = 20 * (ndlls + 1);
    size_t ilt_off[4], iat_off[4], dname_off[4];
    size_t hn_off[4][12];
    for (size_t d = 0; d < ndlls; d++) {
        ilt_off[d] = off;
        off += 8 * (dlls[d].nfns + 1);
    }
    for (size_t d = 0; d < ndlls; d++) {
        iat_off[d] = off;
        off += 8 * (dlls[d].nfns + 1);
    }
    for (size_t d = 0; d < ndlls; d++) {
        dname_off[d] = off;
        size_t l = strlen(dlls[d].name) + 1;
        memcpy(imp + off, dlls[d].name, l);
        off += l;
        for (size_t f = 0; f < dlls[d].nfns; f++) {
            if (off & 1) off++;
            hn_off[d][f] = off;
            w16(imp + off, 0);
            size_t fl = strlen(dlls[d].fns[f]) + 1;
            memcpy(imp + off + 2, dlls[d].fns[f], fl);
            off += 2 + fl;
        }
    }
    size_t gi = 0;
    for (size_t d = 0; d < ndlls; d++) {
        w32(imp + 20 * d + 0, rdata_rva + (uint32_t)ilt_off[d]);   /* OFT */
        w32(imp + 20 * d + 12, rdata_rva + (uint32_t)dname_off[d]);/* Name */
        w32(imp + 20 * d + 16, rdata_rva + (uint32_t)iat_off[d]);  /* FirstThunk */
        for (size_t f = 0; f < dlls[d].nfns; f++) {
            uint32_t h = rdata_rva + (uint32_t)hn_off[d][f];
            w32(imp + ilt_off[d] + 8 * f, h);
            w32(imp + iat_off[d] + 8 * f, h);
            if (gi < iat_cap) iat_out[gi] = rdata_rva + (uint32_t)iat_off[d] + 8 * (uint32_t)f;
            gi++;
        }
    }
    if (dir_rva) *dir_rva = rdata_rva;
    if (dir_size) *dir_size = (uint32_t)(20 * (ndlls + 1));
    return (off + 7) & ~(size_t)7;
}

/* andaço PE32+ comum (2 seções; base 0x00400000; import dir 0x1000). */
static void pe64_skeleton(uint8_t* buf, uint32_t imp_dir_size, uint32_t iat_rva,
                          uint32_t iat_size) {
    memset(buf, 0, 0xA00);
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x8664);
    w16(buf + pe + 6, 2);
    w16(buf + pe + 20, 240);
    w16(buf + pe + 22, 0x0102);
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x20B);
    w32(buf + opt + 16, 0x2000);
    w32(buf + opt + 20, 0x2000);
    w32(buf + opt + 24, 0x00400000);
    w32(buf + opt + 28, 0);
    w32(buf + opt + 32, 0x1000);
    w32(buf + opt + 36, 0x200);
    w32(buf + opt + 56, 0x3000);
    w32(buf + opt + 60, 0x200);
    w16(buf + opt + 68, 3);
    w32(buf + opt + 108, 16);
    w32(buf + opt + 112 + 8, 0x1000);
    w32(buf + opt + 112 + 12, imp_dir_size ? imp_dir_size : 0x3C);
    w32(buf + opt + 112 + 96, iat_rva);
    w32(buf + opt + 112 + 100, iat_size);
    pe_sec_at(buf, 240, 0, ".rdata", 0x400, 0x1000, 0x400, 0x200, 0x40000040);
    pe_sec_at(buf, 240, 1, ".text", 0x400, 0x2000, 0x400, 0x600, 0x60000020);
}

/* variant 7: MEMÓRIA — aloca, escreve padrão, relê, heap, retorna código. */
static size_t build_mem_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    static const char* k32fns[] = {
        "ExitProcess", "VirtualAlloc", "VirtualFree", "GetProcessHeap", "HeapAlloc",
    };
    imp_dll dlls[1] = { { "KERNEL32.dll", {0}, 5 } };
    for (int i = 0; i < 5; i++) dlls[0].fns[i] = k32fns[i];
    pe64_skeleton(buf, 0x3C, 0, 0);
    uint32_t iat[8], dir_rva = 0, dir_size = 0;
    size_t extra = imp_build(buf + 0x200, 0x1000, dlls, 1, iat, 8, &dir_rva, &dir_size);
    (void)extra;
    w32(buf + 0x80 + 24 + 112 + 12, dir_size);
    w32(buf + 0x80 + 24 + 112 + 96, iat[0]);
    w32(buf + 0x80 + 24 + 112 + 100, 6 * 8);
    /* RVA dos dados: nenhum extra */

    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define E32(v) do { uint32_t _i = (uint32_t)(v); memcpy(c + o, &_i, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define INC_EBX() EMIT(0xFF, 0xC3)
#define JNZ_OK() do { EMIT(0x75, 0x02); INC_EBX(); } while (0)  /* jnz +2 sobre dec */
#define JE_OK()  do { EMIT(0x74, 0x02); INC_EBX(); } while (0)

    EMIT(0x53);                        /* push rbx */
    EMIT(0x41, 0x54);                  /* push r12 */
    EMIT(0x41, 0x55);                  /* push r13 */
    EMIT(0x41, 0x56);                  /* push r14 */
    EMIT(0x48, 0x83, 0xEC, 0x50);      /* sub rsp, 0x50 */
    EMIT(0xBB); E32(0);                /* mov ebx, 0 (contador de FALHAS) */

    /* VirtualAlloc(0, 0x1000, 0x3000, 4) */
    EMIT(0x31, 0xC9);                  /* xor ecx, ecx */
    EMIT(0xBA); E32(0x1000);           /* mov edx, 0x1000 */
    EMIT(0x41, 0xB8); E32(0x3000);     /* mov r8d, MEM_COMMIT|RESERVE */
    EMIT(0x41, 0xB9); E32(4);          /* mov r9d, PAGE_READWRITE */
    CALL_IAT(iat[1]);                  /* VirtualAlloc */
    EMIT(0x49, 0x89, 0xC4);            /* mov r12, rax */
    EMIT(0x48, 0x85, 0xC0);            /* test rax, rax */
    JNZ_OK();                          /* checagem 1 */

    /* bloco só se r12 != 0 */
    EMIT(0x4D, 0x85, 0xE4);            /* test r12, r12 */
    EMIT(0x74, 0x38);                  /* jz heap_ch (offset conferido abaixo) */

    /* padrão: [r12]=0x11223344 [r12+8]=0x55667788 [r16]=r12 */
    EMIT(0x41, 0xC7, 0x04, 0x24); E32(0x11223344u);   /* mov dword [r12], imm */
    EMIT(0x49, 0xC7, 0x44, 0x24, 0x08); E32(0x55667788u); /* mov qword [r12+8], imm */
    EMIT(0x4D, 0x89, 0x64, 0x24, 0x10);               /* mov [r12+16], r12 */
    /* checagem 2: qword [r12+8] == 0x55667788 */
    EMIT(0x49, 0x8B, 0x44, 0x24, 0x08);               /* mov rax, [r12+8] */
    EMIT(0x49, 0xBD); E32(0x55667788u); E32(0);       /* mov r13, 0x55667788 */
    EMIT(0x4C, 0x39, 0xE8);                           /* cmp rax, r13 */
    JE_OK();
    /* checagem 3: [r12+16] == r12 */
    EMIT(0x49, 0x8B, 0x44, 0x24, 0x10);               /* mov rax, [r12+16] */
    EMIT(0x4C, 0x39, 0xE0);                           /* cmp rax, r12 */
    JE_OK();

    /* checagem 4: HeapAlloc(GetProcessHeap(), 0, 64) != 0 */
    CALL_IAT(iat[3]);                  /* GetProcessHeap (0 args) */
    EMIT(0x48, 0x89, 0xC1);            /* mov rcx, rax */
    EMIT(0x31, 0xD2);                  /* xor edx, edx */
    EMIT(0x41, 0xB8); E32(64);         /* mov r8d, 64 */
    CALL_IAT(iat[4]);                  /* HeapAlloc */
    EMIT(0x48, 0x85, 0xC0);            /* test rax, rax */
    JNZ_OK();

    /* ExitProcess(ebx) */
    EMIT(0x89, 0xD9);                  /* mov ecx, ebx */
    CALL_IAT(iat[0]);                  /* ExitProcess */
    EMIT(0xF4);

#undef JE_OK
#undef JNZ_OK
#undef INC_EBX
#undef CALL_IAT
#undef E32
#undef EMIT
    (void)o;
    return 0xA00;
}

/* variant 8: GDI + BitBlt + StretchBlt + SetPixel → frame 320x240.
 * bitmap 160x120 (branco/vermelho/azul na metade) escalada 2x p/ a
 * superfície via StretchBlt; recorte BitBlt no canto; pixels de acento. */
static size_t build_visual_stretch_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    static const char* k32fns[] = { "ExitProcess" };
    static const char* gdi8fns[] = {
        "CreateCompatibleDC", "CreateCompatibleBitmap", "SelectObject",
        "CreateSolidBrush", "PatBlt", "BitBlt", "StretchBlt", "SetPixel",
    };
    imp_dll dlls[2];
    memset(dlls, 0, sizeof(dlls));
    dlls[0].name = "KERNEL32.dll"; dlls[0].nfns = 1; dlls[0].fns[0] = k32fns[0];
    dlls[1].name = "gdi32.dll";    dlls[1].nfns = 8;
    for (int i = 0; i < 8; i++) dlls[1].fns[i] = gdi8fns[i];
    pe64_skeleton(buf, 0x3C, 0, 0);
    uint32_t iat[16], dir_rva = 0, dir_size = 0;
    imp_build(buf + 0x200, 0x1000, dlls, 2, iat, 16, &dir_rva, &dir_size);
    w32(buf + 0x80 + 24 + 112 + 12, dir_size);
    w32(buf + 0x80 + 24 + 112 + 96, iat[0]);
    w32(buf + 0x80 + 24 + 112 + 100, (uint32_t)(11 * 8));
    /* iat[0]=ExitProcess; iat[1..8]=gdi na ordem acima */

    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define E32(v) do { uint32_t _i = (uint32_t)(v); memcpy(c + o, &_i, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define MOV_M32(disp8, v) do { EMIT(0xC7, 0x44, 0x24, (uint8_t)(disp8)); E32(v); } while (0)
#define PAINT(colorref, x, y, w, h) do { \
        EMIT(0xB9); E32(colorref); \
        CALL_IAT(iat[4]);                    /* CreateSolidBrush */ \
        EMIT(0x48, 0x89, 0xC2);              /* mov rdx, rax */ \
        EMIT(0x4C, 0x89, 0xE9);              /* mov rcx, r13 */ \
        CALL_IAT(iat[3]);                    /* SelectObject */ \
        EMIT(0x4C, 0x89, 0xE9); \
        EMIT(0xBA); E32(x); \
        EMIT(0x41, 0xB8); E32(y); \
        EMIT(0x41, 0xB9); E32(w); \
        MOV_M32(0x20, (h)); \
        MOV_M32(0x28, 0x00F00021u);          /* PATCOPY */ \
        CALL_IAT(iat[5]);                    /* PatBlt */ \
    } while (0)

    EMIT(0x53); EMIT(0x41, 0x54); EMIT(0x41, 0x55);    /* push rbx/r12/r13 */
    EMIT(0x48, 0x83, 0xEC, 0x60);                      /* sub rsp, 0x60 */

    EMIT(0x31, 0xC9);
    CALL_IAT(iat[1]);                  /* hdcScreen = CreateCompatibleDC(0) */
    EMIT(0x49, 0x89, 0xC4);            /* mov r12, rax */
    EMIT(0x31, 0xC9);
    CALL_IAT(iat[1]);                  /* hdcMem */
    EMIT(0x49, 0x89, 0xC5);            /* mov r13, rax */

    EMIT(0x4C, 0x89, 0xE9);            /* rcx = hdcMem */
    EMIT(0xBA); E32(160);              /* edx = 160 */
    EMIT(0x41, 0xB8); E32(120);        /* r8d = 120 */
    CALL_IAT(iat[2]);                  /* CreateCompatibleBitmap */
    EMIT(0x48, 0x89, 0xC3);            /* mov rbx, rax */
    EMIT(0x4C, 0x89, 0xE9);
    EMIT(0x48, 0x89, 0xDA);            /* rdx = hbm */
    CALL_IAT(iat[3]);                  /* SelectObject(hdcMem, hbm) */

    PAINT(0x00FFFFFFu, 0, 0, 160, 120);     /* branco (metade) */
    PAINT(0x000000FFu, 20, 20, 60, 40);     /* vermelho */
    PAINT(0x00FF0000u, 80, 60, 50, 30);     /* azul */

    /* StretchBlt(hdcScreen, 0,0, 320,240, hdcMem, 0,0, 160,120, SRCCOPY) */
    EMIT(0x4C, 0x89, 0xE1);            /* rcx = hdcScreen */
    EMIT(0x31, 0xD2);                  /* x = 0 */
    EMIT(0x45, 0x31, 0xC0);            /* y = 0 */
    EMIT(0x41, 0xB9); E32(320);        /* cx dst */
    MOV_M32(0x20, 240);                /* cy dst */
    EMIT(0x4C, 0x89, 0x6C, 0x24, 0x28);/* hdcSrc = r13 */
    MOV_M32(0x30, 0);                  /* xSrc */
    MOV_M32(0x38, 0);                  /* ySrc */
    MOV_M32(0x40, 160);                /* cxSrc */
    MOV_M32(0x48, 120);                /* cySrc */
    MOV_M32(0x50, 0x00CC0020u);        /* SRCCOPY */
    CALL_IAT(iat[7]);                  /* StretchBlt */

    /* BitBlt(hdcScreen, 280, 0, 40, 30, hdcMem, 0, 0, SRCCOPY) */
    EMIT(0x4C, 0x89, 0xE1);
    EMIT(0xBA); E32(280);
    EMIT(0x45, 0x31, 0xC0);
    EMIT(0x41, 0xB9); E32(40);
    MOV_M32(0x20, 30);
    EMIT(0x4C, 0x89, 0x6C, 0x24, 0x28);
    MOV_M32(0x30, 0);
    MOV_M32(0x38, 0);
    MOV_M32(0x40, 0x00CC0020u);
    CALL_IAT(iat[6]);                  /* BitBlt */

    /* SetPixel(hdcScreen, 5, 5, verde) e (314, 234, verde) */
    EMIT(0x4C, 0x89, 0xE1);
    EMIT(0xBA); E32(5);
    EMIT(0x41, 0xB8); E32(5);
    EMIT(0x41, 0xB9); E32(0x0000FF00u);
    CALL_IAT(iat[8]);                  /* SetPixel */
    EMIT(0x4C, 0x89, 0xE1);
    EMIT(0xBA); E32(314);
    EMIT(0x41, 0xB8); E32(234);
    EMIT(0x41, 0xB9); E32(0x0000FF00u);
    CALL_IAT(iat[8]);

    EMIT(0x31, 0xC9);
    CALL_IAT(iat[0]);                  /* ExitProcess(0) */
    EMIT(0xF4);

#undef PAINT
#undef MOV_M32
#undef CALL_IAT
#undef E32
#undef EMIT
    (void)o;
    return 0xA00;
}

/* variant 9: Kernel32 + 1 API GDI — verifica módulos/proc/env/systeminfo,
 * desenha (PatBlt+SetPixel), MessageBoxA honesta e encerra com nº de falhas. */
static size_t build_apis_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    static const char* k32fns[] = {
        "ExitProcess", "GetModuleHandleA", "GetProcAddress", "GetCommandLineA",
        "GetEnvironmentVariableA", "GetSystemInfo",
    };
    static const char* gdi5fns[] = {
        "CreateCompatibleDC", "CreateSolidBrush", "SelectObject", "PatBlt", "SetPixel",
    };
    static const char* u32fns[] = { "MessageBoxA" };
    imp_dll dlls[3];
    memset(dlls, 0, sizeof(dlls));
    dlls[0].name = "KERNEL32.dll"; dlls[0].nfns = 6;
    for (int i = 0; i < 6; i++) dlls[0].fns[i] = k32fns[i];
    dlls[1].name = "gdi32.dll";    dlls[1].nfns = 5;
    for (int i = 0; i < 5; i++) dlls[1].fns[i] = gdi5fns[i];
    dlls[2].name = "user32.dll";   dlls[2].nfns = 1; dlls[2].fns[0] = u32fns[0];
    pe64_skeleton(buf, 0x3C, 0, 0);
    uint32_t iat[16], dir_rva = 0, dir_size = 0;
    size_t extra = imp_build(buf + 0x200, 0x1000, dlls, 3, iat, 16, &dir_rva, &dir_size);
    w32(buf + 0x80 + 24 + 112 + 12, dir_size);
    w32(buf + 0x80 + 24 + 112 + 96, iat[0]);
    w32(buf + 0x80 + 24 + 112 + 100, (uint32_t)(15 * 8));

    /* strings extras em .rdata após os nomes de import */
    uint32_t s_tick = 0x1000 + (uint32_t)extra;
    memcpy(buf + 0x200 + extra, "GetTickCount64", 15); extra += 15;
    uint32_t s_env = 0x1000 + (uint32_t)extra;
    memcpy(buf + 0x200 + extra, "PORTICO_TEST", 13); extra += 13;
    uint32_t s_mbtext = 0x1000 + (uint32_t)extra;
    memcpy(buf + 0x200 + extra, "fase5 ok", 9); extra += 9;
    uint32_t s_mbcap = 0x1000 + (uint32_t)extra;
    memcpy(buf + 0x200 + extra, "Portico", 8); extra += 8;
    /* "KERNEL32.dll" reaproveita o nome do descritor: offset 2*(ndlls+1)*10? não
     * — imp_build posiciona o nome do DLL; localizamos via desc[0].Name. */
    uint32_t k32_name_rva = dir_rva; (void)k32_name_rva;
    uint32_t desc0_name = 0;
    memcpy(&desc0_name, buf + 0x200 + 12, 4);   /* desc[0].Name */

    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define E32(v) do { uint32_t _i = (uint32_t)(v); memcpy(c + o, &_i, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define LEA_RDX_TO(rva) do { \
        EMIT(0x48, 0x8D, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define LEA_RCX_TO(rva) do { \
        EMIT(0x48, 0x8D, 0x0D); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define INC_EBX() EMIT(0xFF, 0xC3)
#define JNZ_OK() do { EMIT(0x75, 0x02); INC_EBX(); } while (0)
#define JE_OK()  do { EMIT(0x74, 0x02); INC_EBX(); } while (0)

    EMIT(0x53); EMIT(0x41, 0x54); EMIT(0x41, 0x55); EMIT(0x41, 0x56);
    EMIT(0x48, 0x83, 0xEC, 0x78);                  /* sub rsp, 0x78 */
    EMIT(0xBB); E32(0);                            /* ebx = nº de falhas (0 = ok) */

    /* GetModuleHandleA("KERNEL32.dll") != 0 */
    LEA_RCX_TO(desc0_name);
    CALL_IAT(iat[1]);
    EMIT(0x49, 0x89, 0xC4);                        /* r12 = h */
    EMIT(0x48, 0x85, 0xC0);
    JNZ_OK();

    /* GetProcAddress(h, "GetTickCount64") != 0 */
    EMIT(0x4C, 0x89, 0xE1);                        /* rcx = h */
    LEA_RDX_TO(s_tick);
    CALL_IAT(iat[2]);
    EMIT(0x48, 0x85, 0xC0);
    JNZ_OK();

    /* GetCommandLineA() != 0 */
    CALL_IAT(iat[3]);
    EMIT(0x48, 0x85, 0xC0);
    JNZ_OK();

    /* GetEnvironmentVariableA("PORTICO_TEST", buf=rsp+0x20, 64) == 2 ("ok") */
    LEA_RCX_TO(s_env);
    EMIT(0x48, 0x8D, 0x54, 0x24, 0x20);            /* lea rdx, [rsp+0x20] */
    EMIT(0x41, 0xB8); E32(64);
    CALL_IAT(iat[4]);
    EMIT(0x83, 0xF8, 0x02);                        /* cmp eax, 2 */
    JE_OK();

    /* GetSystemInfo(&info=rsp+0x20): dwPageSize == 4096 */
    LEA_RCX_TO(0); /* preenchido abaixo — placeholder não usado */
    o -= 7;                                        /* desfaz LEA placeholder */
    EMIT(0x48, 0x8D, 0x4C, 0x24, 0x20);            /* lea rcx, [rsp+0x20] */
    CALL_IAT(iat[5]);
    EMIT(0x8B, 0x44, 0x24, 0x24);                  /* mov eax, [rsp+0x24] (dwPageSize) */
    EMIT(0x3D); E32(4096);
    JE_OK();

    /* GDI: CreateCompatibleDC + PatBlt branco + SetPixel */
    EMIT(0x31, 0xC9);
    CALL_IAT(iat[6]);                              /* hdc */
    EMIT(0x49, 0x89, 0xC5);                        /* r13 = hdc */
    EMIT(0xB9); E32(0x00FFFFFFu);                  /* branco */
    CALL_IAT(iat[7]);                              /* CreateSolidBrush */
    EMIT(0x48, 0x89, 0xC2);
    EMIT(0x4C, 0x89, 0xE9);
    CALL_IAT(iat[8]);                              /* SelectObject */
    EMIT(0x4C, 0x89, 0xE9);
    EMIT(0xBA); E32(0); EMIT(0x41, 0xB8); E32(0);
    EMIT(0x41, 0xB9); E32(320);
    EMIT(0xC7, 0x44, 0x24, 0x20); E32(240);
    EMIT(0xC7, 0x44, 0x24, 0x28); E32(0x00F00021u);
    CALL_IAT(iat[9]);                              /* PatBlt */
    EMIT(0x4C, 0x89, 0xE9);
    EMIT(0xBA); E32(10); EMIT(0x41, 0xB8); E32(10);
    EMIT(0x41, 0xB9); E32(0x000000FFu);            /* vermelho COLORREF */
    CALL_IAT(iat[10]);                             /* SetPixel */

    /* MessageBoxA(0, "fase5 ok", "Portico", 0) == 1 (IDOK) */
    EMIT(0x31, 0xC9);
    LEA_RDX_TO(s_mbtext);
    EMIT(0x4C, 0x8D, 0x05); do {
        int32_t _d = (int32_t)(s_mbcap - (0x2000u + (uint32_t)o + 4));
        memcpy(c + o, &_d, 4); o += 4;
    } while (0);                                   /* lea r8, [rip+cap] */
    EMIT(0x45, 0x31, 0xC9);                        /* r9d = 0 */
    CALL_IAT(iat[11]);                             /* MessageBoxA */
    EMIT(0x83, 0xF8, 0x01);                        /* cmp eax, 1 */
    JE_OK();

    EMIT(0x89, 0xD9);                              /* ecx = ebx (falhas) */
    CALL_IAT(iat[0]);                              /* ExitProcess */
    EMIT(0xF4);

#undef JE_OK
#undef JNZ_OK
#undef INC_EBX
#undef LEA_RCX_TO
#undef LEA_RDX_TO
#undef CALL_IAT
#undef E32
#undef EMIT
    (void)o;
    return 0xA00;
}

/* variant 10: CRT — estrutura de runtime do convidado em x64 REAL:
 * mainCRTStartup chama main (call/ret), main tem prologo/epilogo CRT
 * (push rbp / mov rbp,rsp / and rsp,-16 / leave) e exercita as rotinas
 * classicas do CRT implementadas com as novas string ops/SSE:
 *   fn_memcpy  = REP MOVSB (copia 16 bytes da literal)
 *   fn_memset  = REP STOSB (preenche 32 bytes com 0xAB)
 *   fn_strlen  = REPNE SCASB (comprimento == 16)
 *   fn_memcmp  = REPE CMPSB (compara literal x copia)
 *   SSE: PXOR+MOVAPS (zera 16B) e MOVUPS+MOVAPS (copia 16B)
 *   MOVD/MOVQ + loop com LODSB (checksum inteiro == 1235)
 * main retorna em EAX o numero de FALHAS das checagens internas; o entry
 * chama ExitProcess(eax) — exit 0 = tudo verificado. NOTA honesta: nao ha
 * MSVC/MinGW neste ambiente Linux; o codigo e emitido pelo proprio builder
 * replicando o padrao real do CRT (o que compiladores reais emitem). */
static size_t build_crt_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    static const char* k32fns[] = { "ExitProcess" };
    imp_dll dlls[1] = { { "KERNEL32.dll", {0}, 1 } };
    dlls[0].fns[0] = k32fns[0];
    pe64_skeleton(buf, 0x28, 0, 0);
    uint32_t iat[4], dir_rva = 0, dir_size = 0;
    imp_build(buf + 0x200, 0x1000, dlls, 1, iat, 4, &dir_rva, &dir_size);
    w32(buf + 0x80 + 24 + 112 + 12, dir_size);
    w32(buf + 0x80 + 24 + 112 + 96, iat[0]);
    w32(buf + 0x80 + 24 + 112 + 100, 16);           /* IAT dir = 8*(1+1) */
    /* literal em .rdata RVA 0x1100 (raw buf+0x300): "Portico CRT 2026\0" */
    memcpy(buf + 0x300, "Portico CRT 2026", 17);

    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define E32(v) do { uint32_t _i = (uint32_t)(v); memcpy(c + o, &_i, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define LEA_RIP(rm_byte, rva) do { EMIT(0x48, 0x8D, (rm_byte)); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define CALL_TO(at) do { EMIT(0xE8); (at) = o; E32(0); } while (0)
#define PATCH32(at, target) do { \
        int32_t _r = (int32_t)((uint32_t)(target) - ((uint32_t)(at) + 4)); \
        memcpy(c + (at), &_r, 4); } while (0)
#define INC_EBX() EMIT(0xFF, 0xC3)
#define JE_OK()  do { EMIT(0x74, 0x02); INC_EBX(); } while (0)
#define CMP8_SS(disp, imm) EMIT(0x80, 0x7C, 0x24, (disp), (imm))

    /* ============ mainCRTStartup ============ */
    EMIT(0x48, 0x83, 0xEC, 0x28);                   /* sub rsp, 0x28 */
    size_t p_call_main = 0;
    CALL_TO(p_call_main);                           /* call main */
    EMIT(0x89, 0xC1);                               /* mov ecx, eax */
    CALL_IAT(iat[0]);                               /* call [rip+ExitProcess] */
    EMIT(0xF4);                                     /* hlt (nunca chega) */

    /* ============ main (prologo CRT) ============ */
    size_t main_off = o;
    EMIT(0x55);                                     /* push rbp */
    EMIT(0x48, 0x89, 0xE5);                         /* mov rbp, rsp */
    EMIT(0x48, 0x83, 0xE4, 0xF0);                   /* and rsp, -16 */
    EMIT(0x48, 0x81, 0xEC); E32(0xD0);              /* sub rsp, 0xD0 */
    EMIT(0xBB); E32(0);                             /* mov ebx, 0 (falhas) */

    /* -- memcpy([rsp+0x40], STR, 16) -- */
    EMIT(0x48, 0x8D, 0x4C, 0x24, 0x40);             /* lea rcx, [rsp+0x40] */
    LEA_RIP(0x15, 0x1100);                          /* lea rdx, [rip+STR] */
    EMIT(0x49, 0xC7, 0xC0); E32(16);                /* mov r8, 16 */
    size_t p_fn_memcpy = 0;
    CALL_TO(p_fn_memcpy);
    CMP8_SS(0x45, 0x63);                            /* dst[5] == 'c' */
    JE_OK();
    CMP8_SS(0x4F, 0x36);                            /* dst[15] == '6' */
    JE_OK();

    /* -- strlen(STR) == 16 -- */
    LEA_RIP(0x0D, 0x1100);                          /* lea rcx, [rip+STR] */
    size_t p_fn_strlen = 0;
    CALL_TO(p_fn_strlen);
    EMIT(0x48, 0x83, 0xF8, 0x10);                   /* cmp rax, 16 */
    JE_OK();

    /* -- memcmp(STR, dst, 16) == 0 -- */
    LEA_RIP(0x0D, 0x1100);
    EMIT(0x48, 0x8D, 0x54, 0x24, 0x40);             /* lea rdx, [rsp+0x40] */
    EMIT(0x49, 0xC7, 0xC0); E32(16);
    size_t p_fn_memcmp = 0;
    CALL_TO(p_fn_memcmp);
    EMIT(0x85, 0xC0);                               /* test eax, eax */
    JE_OK();

    /* -- memset([rsp+0x60], 0xAB, 32) -- */
    EMIT(0x48, 0x8D, 0x4C, 0x24, 0x60);             /* lea rcx, [rsp+0x60] */
    EMIT(0xBA); E32(0xAB);                          /* mov edx, 0xAB */
    EMIT(0x49, 0xC7, 0xC0); E32(32);                /* mov r8, 32 */
    size_t p_fn_memset = 0;
    CALL_TO(p_fn_memset);
    CMP8_SS(0x60, 0xAB);
    JE_OK();
    CMP8_SS(0x7F, 0xAB);
    JE_OK();

    /* -- SSE: pxor + movaps zera [rsp..rsp+15] -- */
    EMIT(0x66, 0x0F, 0xEF, 0xC0);                   /* pxor xmm0, xmm0 */
    EMIT(0x0F, 0x29, 0x04, 0x24);                   /* movaps [rsp], xmm0 */
    CMP8_SS(0x0F, 0x00);
    JE_OK();

    /* -- SSE: movups load + movaps store copia STR para [rsp+0x10] -- */
    LEA_RIP(0x35, 0x1100);                          /* lea rsi, [rip+STR] */
    EMIT(0x0F, 0x10, 0x0E);                         /* movups xmm1, [rsi] */
    EMIT(0x0F, 0x29, 0x4C, 0x24, 0x10);             /* movaps [rsp+0x10], xmm1 */
    CMP8_SS(0x15, 0x63);
    JE_OK();

    /* -- checksum inteiro via LODSB (soma dos bytes == 1235) -- */
    LEA_RIP(0x35, 0x1100);
    EMIT(0xB9); E32(16);                            /* mov ecx, 16 */
    EMIT(0x45, 0x31, 0xC9);                         /* xor r9d, r9d */
    size_t o_sum = o;
    EMIT(0xAC);                                     /* lodsb */
    EMIT(0x0F, 0xB6, 0xD0);                         /* movzx edx, al */
    EMIT(0x44, 0x01, 0xCA);                         /* add edx, r9d */
    EMIT(0x41, 0x89, 0xD1);                         /* mov r9d, edx */
    EMIT(0xFF, 0xC9);                               /* dec ecx */
    EMIT(0x75, 0x00);                               /* jnz o_sum (patch) */
    size_t o_jmp = o - 1;
    c[o_jmp] = (uint8_t)(int8_t)((int64_t)o_sum - (int64_t)(o_jmp + 1));
    EMIT(0x41, 0x81, 0xF9); E32(0x4D3);             /* cmp r9d, 1235 */
    JE_OK();

    /* -- MOVD/MOVQ: gpr -> xmm -> memoria -- */
    EMIT(0xB8); E32(0x11223344);                    /* mov eax, 0x11223344 */
    EMIT(0x66, 0x0F, 0x6E, 0xC8);                   /* movd xmm1, eax */
    EMIT(0x66, 0x0F, 0xD6, 0x4C, 0x24, 0x20);       /* movq [rsp+0x20], xmm1 */
    EMIT(0x81, 0x7C, 0x24, 0x20); E32(0x11223344);  /* cmp dword [rsp+0x20], imm */
    JE_OK();

    /* -- retorno de main: eax = nº de falhas; leave; ret -- */
    EMIT(0x89, 0xD8);                               /* mov eax, ebx */
    EMIT(0xC9);                                     /* leave */
    EMIT(0xC3);                                     /* ret */

    /* ============ fn_memcpy(dst=rcx, src=rdx, n=r8) = REP MOVSB ============ */
    size_t fn_memcpy = o;
    EMIT(0x48, 0x89, 0xC8);                         /* mov rax, rcx (ret dst) */
    EMIT(0x48, 0x89, 0xD6);                         /* mov rsi, rdx */
    EMIT(0x48, 0x89, 0xCF);                         /* mov rdi, rcx */
    EMIT(0x4C, 0x89, 0xC1);                         /* mov rcx, r8 */
    EMIT(0xFC);                                     /* cld */
    EMIT(0xF3, 0xA4);                               /* rep movsb */
    EMIT(0xC3);                                     /* ret */

    /* ============ fn_memset(dst=rcx, val=rdx, n=r8) = REP STOSB ============ */
    size_t fn_memset = o;
    EMIT(0x49, 0x89, 0xC9);                         /* mov r9, rcx */
    EMIT(0x48, 0x89, 0xD0);                         /* mov rax, rdx (AL=val) */
    EMIT(0x48, 0x89, 0xCF);                         /* mov rdi, rcx */
    EMIT(0x4C, 0x89, 0xC1);                         /* mov rcx, r8 */
    EMIT(0xFC);                                     /* cld */
    EMIT(0xF3, 0xAA);                               /* rep stosb */
    EMIT(0x4C, 0x89, 0xC8);                         /* mov rax, r9 (ret dst) */
    EMIT(0xC3);                                     /* ret */

    /* ============ fn_strlen(s=rcx) = REPNE SCASB ============ */
    size_t fn_strlen = o;
    EMIT(0x49, 0x89, 0xC9);                         /* mov r9, rcx */
    EMIT(0x48, 0x89, 0xCF);                         /* mov rdi, rcx */
    EMIT(0x31, 0xC0);                               /* xor eax, eax (AL=0) */
    EMIT(0x48, 0xC7, 0xC1); E32(0xFFFFFFFF);        /* mov rcx, -1 */
    EMIT(0xFC);                                     /* cld */
    EMIT(0xF2, 0xAE);                               /* repne scasb */
    EMIT(0x48, 0x89, 0xF8);                         /* mov rax, rdi */
    EMIT(0x4C, 0x29, 0xC8);                         /* sub rax, r9 */
    EMIT(0x48, 0xFF, 0xC8);                         /* dec rax */
    EMIT(0xC3);                                     /* ret */

    /* ============ fn_memcmp(a=rcx, b=rdx, n=r8) = REPE CMPSB ============ */
    size_t fn_memcmp = o;
    EMIT(0x48, 0x89, 0xCE);                         /* mov rsi, rcx */
    EMIT(0x48, 0x89, 0xD7);                         /* mov rdi, rdx */
    EMIT(0x4C, 0x89, 0xC1);                         /* mov rcx, r8 */
    EMIT(0xFC);                                     /* cld */
    EMIT(0xF3, 0xA6);                               /* repe cmpsb */
    EMIT(0x0F, 0x95, 0xC0);                         /* setnz al */
    EMIT(0x0F, 0xB6, 0xC0);                         /* movzx eax, al */
    EMIT(0xC3);                                     /* ret */

    PATCH32(p_call_main, main_off);
    PATCH32(p_fn_memcpy, fn_memcpy);
    PATCH32(p_fn_memset, fn_memset);
    PATCH32(p_fn_strlen, fn_strlen);
    PATCH32(p_fn_memcmp, fn_memcmp);

#undef CMP8_SS
#undef JE_OK
#undef INC_EBX
#undef PATCH32
#undef CALL_TO
#undef LEA_RIP
#undef CALL_IAT
#undef E32
#undef EMIT
    (void)o;
    return 0xA00;
}

/* variant 11: DLL PE32+ x64 — exports reais (add/get_answer/twice) + DllMain.
 * Sem imports (não depende de ninguém); COFF flags = EXECUTABLE|32BIT|DLL. */
static size_t build_dll_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    pe64_skeleton(buf, 0x3C, 0, 0);
    w16(buf + 0x80 + 22, 0x2102);          /* EXECUTABLE_IMAGE|32BIT_MACHINE|DLL */
    /* sem import dir (o skeleton fixa 0x1000/0x3C — zera os dois) */
    w32(buf + 0x80 + 24 + 112 + 8, 0);
    w32(buf + 0x80 + 24 + 112 + 12, 0);
    /* export dir em .rdata (RVA 0x1000) */
    w32(buf + 0x80 + 24 + 112 + 0, 0x1000);
    w32(buf + 0x80 + 24 + 112 + 4, 0x80);

    uint8_t* e = buf + 0x200;
    /* IMAGE_EXPORT_DIRECTORY */
    w32(e + 12, 0x1048);       /* Name: "testdll.dll" */
    w32(e + 16, 1);            /* Base */
    w32(e + 20, 3);            /* NumberOfFunctions */
    w32(e + 24, 3);            /* NumberOfNames */
    w32(e + 28, 0x1028);       /* AddressOfFunctions */
    w32(e + 32, 0x1034);       /* AddressOfNames */
    w32(e + 36, 0x1040);       /* AddressOfNameOrdinals */
    /* functions[]: add=0x2006, get_answer=0x200A, twice=0x2010 */
    w32(e + 0x28, 0x2006);
    w32(e + 0x2C, 0x200A);
    w32(e + 0x30, 0x2010);
    /* names[] */
    w32(e + 0x34, 0x1058);
    w32(e + 0x38, 0x105C);
    w32(e + 0x3C, 0x1068);
    /* name ordinals[] */
    w16(e + 0x40, 0);
    w16(e + 0x42, 1);
    w16(e + 0x44, 2);
    /* strings */
    memcpy(e + 0x48, "testdll.dll", 12);
    memcpy(e + 0x58, "add", 4);
    memcpy(e + 0x5C, "get_answer", 11);
    memcpy(e + 0x68, "twice", 6);

    /* código (RVA 0x2000): entry = DllMain */
    uint8_t* t = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(t + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
    EMIT(0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3);  /* 2000 DllMain: mov eax,1; ret */
    EMIT(0x8D, 0x04, 0x11, 0xC3);              /* 2006 add: lea eax,[rcx+rdx]; ret */
    EMIT(0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3);  /* 200A get_answer: mov eax,42; ret */
    EMIT(0x8D, 0x04, 0x09, 0xC3);              /* 2010 twice: lea eax,[rcx+rcx]; ret */
#undef EMIT
    return 0xA00;
}

/* variant 12: EXE x64 que usa a infraestrutura de DLL —
 * LoadLibraryA("testdll.dll") → GetProcAddress add/get_answer → call real →
 * GetModuleHandleA (mesmo handle) → FreeLibrary → ExitProcess(nº falhas). */
static size_t build_dll_loader_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    static const char* k32fns[] = {
        "ExitProcess", "LoadLibraryA", "GetProcAddress", "GetModuleHandleA",
        "FreeLibrary",
    };
    imp_dll dlls[1] = { { "KERNEL32.dll", {0}, 5 } };
    for (int i = 0; i < 5; i++) dlls[0].fns[i] = k32fns[i];
    pe64_skeleton(buf, 0x3C, 0, 0);
    uint32_t iat[8], dir_rva = 0, dir_size = 0;
    imp_build(buf + 0x200, 0x1000, dlls, 1, iat, 8, &dir_rva, &dir_size);
    (void)dir_rva;
    w32(buf + 0x80 + 24 + 112 + 12, dir_size);
    w32(buf + 0x80 + 24 + 112 + 96, iat[0]);
    w32(buf + 0x80 + 24 + 112 + 100, 6 * 8);
    /* strings do convidado em .rdata (RVA 0x11C0+) */
    memcpy(buf + 0x3C0, "testdll.dll", 12);    /* RVA 0x11C0 */
    memcpy(buf + 0x3CC, "add", 4);             /* RVA 0x11CC */
    memcpy(buf + 0x3D0, "get_answer", 11);     /* RVA 0x11D0 */

    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define LEA_RCX_TO(rva) do { \
        EMIT(0x48, 0x8D, 0x0D); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define LEA_RDX_TO(rva) do { \
        EMIT(0x48, 0x8D, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define INC_EBX() EMIT(0xFF, 0xC3)

    size_t l_q2 = 0, l_q4 = 0, l_qend = 0, f_end = 0, f_q2 = 0, f_q4 = 0;

    EMIT(0x53);                        /* push rbx */
    EMIT(0x41, 0x54);                  /* push r12 */
    EMIT(0x41, 0x55);                  /* push r13 */
    EMIT(0xBB); EMIT(0x00, 0x00, 0x00, 0x00);   /* mov ebx, 0 (falhas) */

    LEA_RCX_TO(0x11C0);                /* "testdll.dll" */
    CALL_IAT(iat[1]);                  /* LoadLibraryA */
    EMIT(0x48, 0x85, 0xC0);            /* test rax, rax */
    EMIT(0x75, 0x04);                  /* jnz ok (sobre inc+jmp) */
    INC_EBX();
    EMIT(0xEB, 0x00); f_end = o - 1;   /* jmp Q_END (sem DLL: encerra) */
    EMIT(0x49, 0x89, 0xC4);            /* mov r12, rax (handle) */

    EMIT(0x4C, 0x89, 0xE1);            /* mov rcx, r12 */
    LEA_RDX_TO(0x11CC);                /* "add" */
    CALL_IAT(iat[2]);                  /* GetProcAddress */
    EMIT(0x48, 0x85, 0xC0);
    EMIT(0x75, 0x04);
    INC_EBX();
    EMIT(0xEB, 0x00); f_q2 = o - 1;    /* jmp Q2 */
    EMIT(0x49, 0x89, 0xC5);            /* mov r13, rax */
    EMIT(0xB9, 0x03, 0x00, 0x00, 0x00);/* mov ecx, 3 */
    EMIT(0xBA, 0x05, 0x00, 0x00, 0x00);/* mov edx, 5 */
    EMIT(0x41, 0xFF, 0xD5);            /* call r13  -> 8 */
    EMIT(0x83, 0xF8, 0x08);            /* cmp eax, 8 */
    EMIT(0x74, 0x02);                  /* je +2 */
    INC_EBX();                         /* Q2: */
    l_q2 = o;

    EMIT(0x4C, 0x89, 0xE1);            /* mov rcx, r12 */
    LEA_RDX_TO(0x11D0);                /* "get_answer" */
    CALL_IAT(iat[2]);                  /* GetProcAddress */
    EMIT(0x48, 0x85, 0xC0);
    EMIT(0x75, 0x04);
    INC_EBX();
    EMIT(0xEB, 0x00); f_q4 = o - 1;    /* jmp Q4 */
    EMIT(0xFF, 0xD0);                  /* call rax -> 42 */
    EMIT(0x83, 0xF8, 0x2A);            /* cmp eax, 42 */
    EMIT(0x74, 0x02);
    INC_EBX();                         /* Q4: */
    l_q4 = o;

    LEA_RCX_TO(0x11C0);                /* "testdll.dll" */
    CALL_IAT(iat[3]);                  /* GetModuleHandleA */
    EMIT(0x4C, 0x39, 0xE0);            /* cmp rax, r12 */
    EMIT(0x74, 0x02);
    INC_EBX();

    EMIT(0x4C, 0x89, 0xE1);            /* mov rcx, r12 */
    CALL_IAT(iat[4]);                  /* FreeLibrary */
    EMIT(0x48, 0x85, 0xC0);
    EMIT(0x75, 0x02);                  /* jnz +2 */
    INC_EBX();

    l_qend = o;                        /* Q_END: */
    EMIT(0x89, 0xD9);                  /* mov ecx, ebx */
    CALL_IAT(iat[0]);                  /* ExitProcess(ebx) */
    EMIT(0xF4);                        /* hlt */
    /* patches rel8 (distâncias calculadas) */
    c[f_end] = (uint8_t)(int8_t)((int)l_qend - (int)(f_end + 1));
    c[f_q2]  = (uint8_t)(int8_t)((int)l_q2  - (int)(f_q2  + 1));
    c[f_q4]  = (uint8_t)(int8_t)((int)l_q4  - (int)(f_q4  + 1));
#undef EMIT
#undef LEA_RCX_TO
#undef LEA_RDX_TO
#undef CALL_IAT
#undef INC_EBX
    return 0xA00;
}

/* variant 13: VFS e2e — CreateFileA("nota.txt") CREATE_ALWAYS, WriteFile,
 * CloseHandle, ExitProcess(nº falhas). Exercita o filesystem virtual do
 * Windows para a raiz do sandbox (FASE 7). */
static size_t build_vfs_x64(uint8_t* buf, size_t cap) {
    if (cap < 0xA00) return 0;
    static const char* k32fns[] = {
        "ExitProcess", "CreateFileA", "WriteFile", "CloseHandle",
    };
    imp_dll dlls[1] = { { "KERNEL32.dll", {0}, 4 } };
    for (int i = 0; i < 4; i++) dlls[0].fns[i] = k32fns[i];
    pe64_skeleton(buf, 0x3C, 0, 0);
    uint32_t iat[8], dir_rva = 0, dir_size = 0;
    imp_build(buf + 0x200, 0x1000, dlls, 1, iat, 8, &dir_rva, &dir_size);
    (void)dir_rva;
    w32(buf + 0x80 + 24 + 112 + 12, dir_size);
    w32(buf + 0x80 + 24 + 112 + 96, iat[0]);
    w32(buf + 0x80 + 24 + 112 + 100, 5 * 8);
    memcpy(buf + 0x3C0, "nota.txt", 9);            /* RVA 0x11C0 */
    memcpy(buf + 0x3D0, "Portico VFS E2E", 16);    /* RVA 0x11D0 */

    uint8_t* c = buf + 0x600;
    size_t o = 0;
#define EMIT(...) do { static const uint8_t _b[] = { __VA_ARGS__ }; \
        memcpy(c + o, _b, sizeof(_b)); o += sizeof(_b); } while (0)
#define LEA_RCX_TO(rva) do { \
        EMIT(0x48, 0x8D, 0x0D); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define LEA_RDX_TO(rva) do { \
        EMIT(0x48, 0x8D, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define CALL_IAT(iat_rva) do { \
        EMIT(0xFF, 0x15); \
        int32_t _d = (int32_t)((uint32_t)(iat_rva) - (0x2000u + (uint32_t)o + 4)); \
        memcpy(c + o, &_d, 4); o += 4; } while (0)
#define FAIL_EXIT() do { EMIT(0xFF, 0xC3); EMIT(0x89, 0xD9); \
        CALL_IAT(iat[0]); EMIT(0xF4); } while (0)

    EMIT(0x53);                        /* push rbx */
    EMIT(0x41, 0x55);                  /* push r13 */
    EMIT(0xBB); EMIT(0x00, 0x00, 0x00, 0x00);   /* mov ebx, 0 (falhas) */
    LEA_RCX_TO(0x11C0);                /* "nota.txt" */
    EMIT(0xBA, 0x00, 0x00, 0x00, 0xC0);/* mov edx, 0xC0000000 (RW) */
    EMIT(0x4D, 0x31, 0xC0);            /* xor r8, r8 (share) */
    EMIT(0x4D, 0x31, 0xC9);            /* xor r9, r9 (sec) */
    EMIT(0x48, 0x83, 0xEC, 0x38);      /* sub rsp, 0x38 (shadow+3 args) */
    EMIT(0x48, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00); /* [rsp+20]=2 CREATE_ALWAYS */
    EMIT(0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x00, 0x00);
    EMIT(0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00);
    CALL_IAT(iat[1]);                  /* CreateFileA */
    /* sem add rsp: o frame (shadow + args) fica até o ExitProcess — APIs com
     * 5º argumento (WriteFile!overlapped) são lidas pelo trap em [rsp+0x20] */
    EMIT(0x48, 0x83, 0xF8, 0xFF);      /* cmp rax, -1 */
    EMIT(0x75, 0x0B);                  /* jne OK1 (sobre FAIL_EXIT=11) */
    FAIL_EXIT();                       /* sem handle: falha e encerra */
    /* OK1: */
    EMIT(0x49, 0x89, 0xC5);            /* mov r13, rax (handle) */
    EMIT(0x4C, 0x89, 0xE9);            /* mov rcx, r13 */
    LEA_RDX_TO(0x11D0);                /* "Portico VFS E2E" */
    EMIT(0x41, 0xB8, 0x0F, 0x00, 0x00, 0x00); /* mov r8d, 15 */
    EMIT(0x4D, 0x31, 0xC9);            /* xor r9, r9 (written=NULL) */
    CALL_IAT(iat[2]);                  /* WriteFile */
    EMIT(0x48, 0x85, 0xC0);            /* test rax, rax */
    EMIT(0x75, 0x0B);                  /* jne OK2 */
    FAIL_EXIT();
    /* OK2: */
    EMIT(0x4C, 0x89, 0xE9);            /* mov rcx, r13 */
    CALL_IAT(iat[3]);                  /* CloseHandle */
    EMIT(0x48, 0x85, 0xC0);
    EMIT(0x75, 0x0B);                  /* jne OK3 */
    FAIL_EXIT();
    /* OK3: */
    EMIT(0x89, 0xD9);                  /* mov ecx, ebx */
    CALL_IAT(iat[0]);                  /* ExitProcess(ebx) */
    EMIT(0xF4);
#undef EMIT
#undef LEA_RCX_TO
#undef LEA_RDX_TO
#undef CALL_IAT
#undef FAIL_EXIT
    return 0xA00;
}

pr_status pr_winhello_build(int variant, void** out_data, size_t* out_len) {
    if (!out_data || !out_len) return PR_ERR_INVALID;
    uint8_t* buf = (uint8_t*)malloc(0x1000);
    if (!buf) return PR_ERR_NOMEM;
    size_t n = 0;
    switch (variant) {
        case 0: n = build_hello(buf, 0x600); break;
        case 1: n = build_gdi(buf, 0x600); break;
        case 2: n = build_pe32plus_imports(buf, 0x800); break;
        case 3: n = build_ordinal(buf, 0x800); break;
        case 4: n = build_reloc32(buf, 0x800); break;
        case 5: n = build_reloc64(buf, 0x800); break;
        case 6: n = build_visual_x64(buf, 0x1000); break;
        case 7: n = build_mem_x64(buf, 0x1000); break;
        case 8: n = build_visual_stretch_x64(buf, 0x1000); break;
        case 9: n = build_apis_x64(buf, 0x1000); break;
        case 10: n = build_crt_x64(buf, 0x1000); break;
        case 11: n = build_dll_x64(buf, 0x1000); break;
        case 12: n = build_dll_loader_x64(buf, 0x1000); break;
        case 13: n = build_vfs_x64(buf, 0x1000); break;
        default: free(buf); return PR_ERR_INVALID;
    }
    if (!n) { free(buf); return PR_ERR_NOMEM; }
    *out_data = buf;
    *out_len = n;
    return PR_OK;
}
