/* Construtor de PE32 mínimo para testes do parser. */
#ifndef PT_PE_BUILDER_H
#define PT_PE_BUILDER_H

#include <stdint.h>
#include <string.h>

/* Escreve em buf (>= 0x400 bytes) um PE32 válido com 1 seção e 1 DLL importada
 * ("KERNEL32.dll"). Retorna o tamanho do arquivo. */
static size_t pt_build_pe32(uint8_t* buf, size_t cap, int as_dll) {
    const size_t FILE_SIZE = 0x400;
    if (cap < FILE_SIZE) return 0;
    memset(buf, 0, FILE_SIZE);

    /* DOS header */
    buf[0] = 'M'; buf[1] = 'Z';
    uint32_t lfanew = 0x80;
    memcpy(buf + 0x3C, &lfanew, 4);

    /* PE signature + COFF */
    uint32_t pe = 0x80;
    memcpy(buf + pe, "PE\0\0", 4);
    uint16_t machine = 0x014C;      /* i386 */
    uint16_t nsec = 1;
    uint16_t optsize = 224;         /* PE32 */
    uint16_t chars = as_dll ? 0x2000u : 0x0102u;
    memcpy(buf + pe + 4, &machine, 2);
    memcpy(buf + pe + 6, &nsec, 2);
    memcpy(buf + pe + 20, &optsize, 2);
    memcpy(buf + pe + 22, &chars, 2);

    /* Optional header PE32 */
    uint32_t opt = pe + 24;
    uint16_t magic = 0x10B;
    memcpy(buf + opt, &magic, 2);
    uint32_t entry = 0x1000;
    memcpy(buf + opt + 16, &entry, 4);
    uint32_t base_of_code = 0x1000;
    memcpy(buf + opt + 20, &base_of_code, 4);
    uint32_t base_of_data = 0x1000;
    memcpy(buf + opt + 24, &base_of_data, 4);
    uint32_t image_base = 0x00400000;
    memcpy(buf + opt + 28, &image_base, 4);
    uint32_t sect_align = 0x1000, file_align = 0x200;
    memcpy(buf + opt + 32, &sect_align, 4);
    memcpy(buf + opt + 36, &file_align, 4);
    uint32_t size_of_image = 0x2000, size_of_headers = 0x200;
    memcpy(buf + opt + 56, &size_of_image, 4);
    memcpy(buf + opt + 60, &size_of_headers, 4);
    uint16_t subsystem = 2; /* GUI */
    memcpy(buf + opt + 68, &subsystem, 2);
    uint32_t num_dd = 16;
    memcpy(buf + opt + 92, &num_dd, 4);
    /* data directory[1] = import */
    uint32_t imp_rva = 0x1000, imp_size = 0x100;
    memcpy(buf + opt + 96 + 8, &imp_rva, 4);
    memcpy(buf + opt + 96 + 12, &imp_size, 4);

    /* Section header (após optional) */
    uint32_t sec = opt + 224;
    memcpy(buf + sec, ".rdata\0\0", 8);
    uint32_t vsize = 0x200, va = 0x1000, rawsize = 0x200, rawptr = 0x200;
    memcpy(buf + sec + 8, &vsize, 4);
    memcpy(buf + sec + 12, &va, 4);
    memcpy(buf + sec + 16, &rawsize, 4);
    memcpy(buf + sec + 20, &rawptr, 4);
    uint32_t schar = 0x40000040;
    memcpy(buf + sec + 36, &schar, 4);

    /* Import directory no offset 0x200 (RVA 0x1000) */
    uint32_t ilt_rva = 0x1020, name_rva = 0x1030, iat_rva = 0x1028;
    uint8_t* imp = buf + 0x200;
    memcpy(imp + 0, &ilt_rva, 4);
    memcpy(imp + 12, &name_rva, 4);
    memcpy(imp + 16, &iat_rva, 4);
    /* descritor final já zerado pelo memset */
    const char* dll = "KERNEL32.dll";
    memcpy(buf + 0x230, dll, strlen(dll) + 1);
    return FILE_SIZE;
}

#endif /* PT_PE_BUILDER_H */
