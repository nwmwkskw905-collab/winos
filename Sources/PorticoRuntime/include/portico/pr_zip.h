/* Portico Runtime — leitura de arquivos ZIP (central directory + inflate via zlib).
 * Suporta entradas "stored" (0) e "deflate" (8). */
#ifndef PORTICO_PR_ZIP_H
#define PORTICO_PR_ZIP_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_zip_entry {
    char     name[256];
    uint32_t comp_method;   /* 0 = stored, 8 = deflate */
    uint32_t crc32;
    uint64_t comp_size;
    uint64_t uncomp_size;
    uint64_t local_header_off;
    int      is_dir;
} pr_zip_entry;

typedef struct pr_zip pr_zip;
typedef void (*pr_log_hook_fn)(void* ud, const char* msg);

/* Abre um ZIP residente em memória. O buffer deve viver durante o handle. */
pr_status pr_zip_open_mem(const void* data, size_t len, pr_zip** out);
/* Lê o arquivo inteiro e abre (para imports no sandbox). */
pr_status pr_zip_open_file(const char* path, pr_zip** out);
void      pr_zip_close(pr_zip* z);

size_t    pr_zip_count(const pr_zip* z);
pr_status pr_zip_entry_info(const pr_zip* z, size_t idx, pr_zip_entry* out);

/* Extrai a entrada (descomprimindo se necessário). *out_data é malloc; usar pr_zip_free_mem.
 * Não faz validação de caminho (path traversal) aqui — o chamador deve sanitizar nomes. */
pr_status pr_zip_extract(const pr_zip* z, size_t idx, void** out_data, size_t* out_len);
void      pr_zip_free_mem(void* p);

/* Extração direta para diretório, com sanitização de nomes (bloqueia "..", "/" absoluto).
 * out_files recebe o nº de arquivos gravados. */
pr_status pr_zip_extract_to_dir(const pr_zip* z, const char* dir,
                                size_t* out_files, pr_log_hook_fn hook, void* hook_ud);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_ZIP_H */
