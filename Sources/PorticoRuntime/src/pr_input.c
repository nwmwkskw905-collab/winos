/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_input.h"
#include <string.h>

void pr_input_clear(pr_input_state* st) {
    if (st) memset(st, 0, sizeof(*st));
}
