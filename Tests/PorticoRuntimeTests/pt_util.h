/* Harness mínimo de testes C do PorticoRuntime. */
#ifndef PT_UTIL_H
#define PT_UTIL_H

#include <stdio.h>
#include <string.h>

extern int pt_failures;
extern int pt_checks;

#define CHECK(cond) do { \
    pt_checks++; \
    if (!(cond)) { \
        pt_failures++; \
        printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ_U32(a, b) do { \
    pt_checks++; \
    unsigned long _va = (unsigned long)(a), _vb = (unsigned long)(b); \
    if (_va != _vb) { \
        pt_failures++; \
        printf("FALHA %s:%d: %s (0x%lx) != %s (0x%lx)\n", \
               __FILE__, __LINE__, #a, _va, #b, _vb); \
    } \
} while (0)

#define CHECK_STR(a, b) do { \
    pt_checks++; \
    if (strcmp((a), (b)) != 0) { \
        pt_failures++; \
        printf("FALHA %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
    } \
} while (0)

#define RUN(fn) do { printf("-- %s\n", #fn); fn(); } while (0)

#endif /* PT_UTIL_H */
