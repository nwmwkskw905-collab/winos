/* Portico Runtime — leitura e CARGA de imagens PE/COFF (executáveis Windows .exe/.dll).
 * Parser/loader próprio: cabeçalho MZ/PE, seções, importações, exports,
 * mapeamento da imagem em memória e diagnóstico legível.
 *
 * IMPORTANTE: esta camada APENAS analisa e carrega em memória para inspeção.
 * NENHUM código da imagem é executado aqui (a execução acontece, no futuro,
 * via interpretador/CPU + camada Win32 — ver docs/BACKEND_INTEGRATION.md). */
#ifndef PORTICO_PR_PE_H
#define PORTICO_PR_PE_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PR_PE_MAX_SECTIONS 96
#define PR_PE_MAX_NAME 64

typedef struct pr_pe_section {
    char     name[9];
    uint32_t virtual_size;
    uint32_t virtual_addr;
    uint32_t raw_size;
    uint32_t raw_ptr;
    uint32_t characteristics;
} pr_pe_section;

typedef struct pr_pe_info {
    int      is_pe;
    int      is_pe32plus;   /* PE32+ (64 bits) */
    int      is_dll;
    uint16_t machine;       /* 0x014c i386, 0x8664 amd64, ... */
    uint16_t subsystem;     /* 2 = GUI, 3 = console */
    uint16_t dll_characteristics;
    uint32_t timestamp;
    uint32_t image_base;    /* PE32; 0 em PE32+ */
    uint64_t image_base64;  /* base preferida completa (PE32 e PE32+) */
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t reloc_rva;     /* diretório Basereloc (índice 5); 0 = sem */
    uint32_t reloc_size;
    uint32_t size_of_image;
    uint32_t entry_point_rva;
    uint32_t section_count;
    uint32_t import_count;  /* nº de DLLs importadas */
    char     arch[16];      /* "x86", "x86_64", "arm", "arm64", "desconhecido" */
} pr_pe_info;

/* Uma entrada da tabela de relocations (Basereloc). */
typedef struct pr_pe_reloc {
    uint32_t rva;   /* RVA do slot a ajustar */
    uint8_t  type;  /* 0 ABSOLUTE (ignorado), 3 HIGHLOW, 10 DIR64 */
} pr_pe_reloc;

/* Uma função importada (por nome ou ordinal). */
typedef struct pr_pe_import_func {
    char     name[PR_PE_MAX_NAME]; /* vazio se by_ordinal */
    uint16_t ordinal;
    uint16_t hint;
    int      by_ordinal;
} pr_pe_import_func;

/* Um export do diretório de exports. */
typedef struct pr_pe_export {
    char     name[PR_PE_MAX_NAME]; /* vazio se exportado apenas por ordinal */
    uint16_t ordinal;
    uint32_t rva;
    int      by_name;
} pr_pe_export;

/* Imagem PE mapeada em memória (CARGA para inspeção — sem execução). */
typedef struct pr_pe_loaded {
    uint8_t*      image;        /* buffer malloc de image_size bytes */
    size_t        image_size;
    uint32_t      entry_rva;
    uint32_t      image_base;   /* PE32; em PE32+ os 32 bits baixos */
    uint64_t      image_base64; /* base preferida completa (PE32 e PE32+) */
    uint32_t      section_alignment;
    uint32_t      file_alignment;
    uint32_t      reloc_rva;
    uint32_t      reloc_size;
    int           is_pe32plus;
    int           is_dll;
    uint16_t      machine;
    uint16_t      subsystem;
    uint32_t      section_count;
    pr_pe_section sections[PR_PE_MAX_SECTIONS];
    char          arch[16];
    char          module_name[PR_PE_MAX_NAME]; /* nome do arquivo sugerido */
} pr_pe_loaded;

/* 1 se buffer começa com "MZ". */
int pr_pe_looks_like(const void* data, size_t len);

/* Analisa a imagem. PR_ERR_FORMAT se não for PE válido. */
pr_status pr_pe_scan(const void* data, size_t len, pr_pe_info* out);

/* Seções: retorna nº copiado (atualiza out_count com o total). */
size_t pr_pe_sections(const void* data, size_t len, pr_pe_section* out,
                      size_t max, size_t* out_count);

/* Nome da i-ésima DLL importada (ordem do diretório). PR_ERR_RANGE se fora. */
pr_status pr_pe_import_name(const void* data, size_t len, size_t index,
                            char* out, size_t out_len);

/* Converte RVA em offset no arquivo. */
pr_status pr_pe_rva_to_offset(const void* data, size_t len, uint32_t rva,
                              uint32_t* out_off);

/* ---- Detalhe de importações (por função) ---- */

/* Nº de funções importadas da index-ésima DLL (0 se fora de faixa). */
size_t pr_pe_import_func_count(const void* data, size_t len, size_t dll_index);

/* Detalhe da func_index-ésima função importada da dll_index-ésima DLL. */
pr_status pr_pe_import_func_at(const void* data, size_t len,
                               size_t dll_index, size_t func_index,
                               pr_pe_import_func* out);

/* RVA do slot IAT da func_index-ésima função da dll_index-ésima DLL
 * (FirstThunk + índice × 4/8). O loader grava o endereço resolvido aqui. */
pr_status pr_pe_import_iat_rva(const void* data, size_t len,
                               size_t dll_index, size_t func_index,
                               uint32_t* out_rva);

/* ---- Exports ---- */

/* Lista exports (até max). Retorna nº copiado; out_count = total. */
/* Lê uma Data Directory pelo índice (0=export ... 3=exception .pdata ...
 * 5=basereloc ...). 0/0 quando ausente. */
pr_status pr_pe_data_dir(const void* data, size_t len, unsigned dir_index,
                         uint32_t* rva, uint32_t* size);

size_t pr_pe_exports(const void* data, size_t len, pr_pe_export* out,
                     size_t max, size_t* out_count);

/* ---- Relocations (diretório Basereloc, índice 5) ---- */

/* Nº total de entradas (inclui ABSOLUTE, que são ignoradas na aplicação). */
size_t pr_pe_reloc_count(const void* data, size_t len);

/* i-ésima entrada (ordem de bloco/entrada do diretório). */
pr_status pr_pe_reloc_at(const void* data, size_t len, size_t index,
                         pr_pe_reloc* out);

/* Valida toda a tabela (estrutura, limites e tipos). Tipos aceitos: 0, 3, 10.
 * PR_ERR_FORMAT com out_reason preenchido (se fornecido). */
pr_status pr_pe_reloc_validate(const void* data, size_t len,
                               char* out_reason, size_t cap);

/* Aplica relocations em `image` (imagem mapeada, image_size bytes):
 * delta = load_base − preferred_base. Valida limites/tipos; slots fora da
 * imagem ou tipos não suportados → PR_ERR_FORMAT com out_reason. */
pr_status pr_pe_reloc_apply(const void* data, size_t len, uint8_t* image,
                            size_t image_size, uint64_t preferred_base,
                            uint64_t load_base, char* out_reason, size_t cap);

/* ---- Carga/mapeamento em memória (SEM execução) ---- */

/* Mapeia a imagem em um buffer contíguo (headers + seções nos RVAs).
 * Não aplica relocations nem resolve imports — é a etapa de carga para
 * inspeção/ligação futura. PR_ERR_NOMEM/PR_ERR_FORMAT/PR_ERR_RANGE possíveis. */
pr_status pr_pe_load(const void* data, size_t len, pr_pe_loaded* out,
                     const char* module_name);

/* Libera o buffer da imagem carregada (idempotente). */
void pr_pe_loaded_free(pr_pe_loaded* img);

/* Ponteiro para o entry point dentro da imagem mapeada (inspeção; NÃO executar).
 * NULL se o RVA estiver fora da imagem. */
void* pr_pe_loaded_entry(const pr_pe_loaded* img);

/* Lê uma string ASCII terminada em NUL apontada por RVA dentro da imagem
 * carregada. */
pr_status pr_pe_loaded_string(const pr_pe_loaded* img, uint32_t rva,
                              char* out, size_t out_len);

/* Converte RVA → ponteiro na imagem carregada (valida limites). */
void* pr_pe_loaded_ptr(const pr_pe_loaded* img, uint32_t rva, size_t len);

/* ---- Diagnóstico legível ---- */

/* Escreve relatório em texto (pt-BR) sobre validação, arquitetura, seções,
 * entry point, imports/exports e avisos. Retorna nº de bytes escritos
 * (sem o NUL). Sempre termina o buffer. */
size_t pr_pe_diagnose(const void* data, size_t len, char* out, size_t out_len);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_PE_H */
