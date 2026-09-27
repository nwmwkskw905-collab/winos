/* Construtor de PE32 para testes do loader: 2 DLLs importadas
 * (KERNEL32.dll com 1 nome + 1 ordinal; USER32.dll com 1 nome),
 * exports (Alpha/Beta) e 2 seções. Não altera pt_pe_builder.h. */
#ifndef PT_PE_BUILDER2_H
#define PT_PE_BUILDER2_H

#include <stdint.h>
#include <string.h>

static void w16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }
static void w32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }

/* buf >= 0x600; retorna tamanho do arquivo (0x600). */
static size_t pt_build_pe_full(uint8_t* buf, size_t cap) {
    const size_t FILE_SIZE = 0x600;
    if (cap < FILE_SIZE) return 0;
    memset(buf, 0, FILE_SIZE);

    /* DOS */
    buf[0] = 'M'; buf[1] = 'Z';
    w32(buf + 0x3C, 0x80);

    /* PE + COFF: 2 seções, PE32 */
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    w16(buf + pe + 4, 0x014C);   /* i386 */
    w16(buf + pe + 6, 2);        /* seções */
    w16(buf + pe + 20, 224);     /* optional size */
    w16(buf + pe + 22, 0x0102);  /* executable */

    /* Optional PE32 */
    uint32_t opt = pe + 24;
    w16(buf + opt, 0x10B);
    w32(buf + opt + 16, 0x1000);      /* entry */
    w32(buf + opt + 20, 0x1000);
    w32(buf + opt + 24, 0x1000);
    w32(buf + opt + 28, 0x00400000);  /* image base */
    w32(buf + opt + 32, 0x1000);
    w32(buf + opt + 36, 0x200);
    w32(buf + opt + 56, 0x3000);      /* size of image */
    w32(buf + opt + 60, 0x200);       /* size of headers */
    w16(buf + opt + 68, 3);           /* console */
    w32(buf + opt + 92, 16);          /* nº data directories */
    w32(buf + opt + 96 + 0, 0x2000);  /* dd[0] exports rva */
    w32(buf + opt + 96 + 4, 0x80);
    w32(buf + opt + 96 + 8, 0x1000);  /* dd[1] imports rva */
    w32(buf + opt + 96 + 12, 0x140);

    /* Seções */
    uint32_t sec = opt + 224;
    memcpy(buf + sec, ".rdata\0\0", 8);
    w32(buf + sec + 8, 0x200);
    w32(buf + sec + 12, 0x1000);
    w32(buf + sec + 16, 0x200);
    w32(buf + sec + 20, 0x200);
    w32(buf + sec + 36, 0x40000040);
    memcpy(buf + sec + 40, ".edata\0\0", 8);
    w32(buf + sec + 48, 0x200);
    w32(buf + sec + 52, 0x2000);
    w32(buf + sec + 56, 0x200);
    w32(buf + sec + 60, 0x400);
    w32(buf + sec + 76, 0x40000040);

    /* Imports (raw 0x200 = RVA 0x1000) */
    uint8_t* imp = buf + 0x200;
    w32(imp + 0x00, 0x1040); /* desc1 OFT */
    w32(imp + 0x0C, 0x1060); /* desc1 name */
    w32(imp + 0x10, 0x1050); /* desc1 FT */
    w32(imp + 0x14, 0x1070); /* desc2 OFT */
    w32(imp + 0x20, 0x1088); /* desc2 name */
    w32(imp + 0x24, 0x1080); /* desc2 FT */
    /* ILT dll1: GetTickCount64 (nome) + ordinal 5 */
    w32(imp + 0x40, 0x10A0);
    w32(imp + 0x44, 0x80000005u);
    /* IAT dll1 (cópia) */
    w32(imp + 0x50, 0x10A0);
    w32(imp + 0x54, 0x80000005u);
    memcpy(imp + 0x60, "KERNEL32.dll", 13);
    /* ILT dll2: MessageBoxA */
    w32(imp + 0x70, 0x10C0);
    w32(imp + 0x80, 0x10C0);
    memcpy(imp + 0x88, "USER32.dll", 11);
    w16(imp + 0xA0, 7);
    memcpy(imp + 0xA2, "GetTickCount64", 15);
    w16(imp + 0xC0, 3);
    memcpy(imp + 0xC2, "MessageBoxA", 12);

    /* Exports (raw 0x400 = RVA 0x2000) */
    uint8_t* exp = buf + 0x400;
    w32(exp + 12, 0x2040);   /* nome do módulo */
    w32(exp + 16, 1);        /* ordinal base */
    w32(exp + 20, 2);        /* nº funções */
    w32(exp + 24, 2);        /* nº nomes */
    w32(exp + 28, 0x2050);   /* AddressOfFunctions */
    w32(exp + 32, 0x2058);   /* AddressOfNames */
    w32(exp + 36, 0x2060);   /* AddressOfNameOrdinals */
    memcpy(exp + 0x40, "game.dll", 9);
    w32(exp + 0x50, 0x2070); /* Alpha */
    w32(exp + 0x54, 0x2080); /* Beta */
    w32(exp + 0x58, 0x2090); /* "Alpha" */
    w32(exp + 0x5C, 0x2098); /* "Beta" */
    w16(exp + 0x60, 0);      /* ord idx Alpha */
    w16(exp + 0x62, 1);      /* ord idx Beta */
    exp[0x70] = 0xC3;        /* stub: ret */
    exp[0x80] = 0xC3;
    memcpy(exp + 0x90, "Alpha", 6);
    memcpy(exp + 0x98, "Beta", 5);
    return FILE_SIZE;
}

#endif /* PT_PE_BUILDER2_H */
