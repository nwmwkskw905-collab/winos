// PorticoRuntime umbrella header — expõe todos os módulos C para Swift
// Licença: projeto próprio (ver LICENSES.md)
#ifndef PORTICO_RUNTIME_H
#define PORTICO_RUNTIME_H

#import <Foundation/Foundation.h>

// Tipos e status
#include "portico/pr_types.h"
#include "portico/pr_log.h"
#include "portico/pr_vm.h"
#include "portico/pr_cap.h"
#include "portico/pr_asm.h"
#include "portico/pr_cpu.h"
#include "portico/pr_cpu64.h"
#include "portico/pr_unwind.h"
#include "portico/pr_pe.h"
#include "portico/pr_peproc.h"
#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_gfx.h"
#include "portico/pr_gl.h"
#include "portico/pr_input.h"
#include "portico/pr_audio.h"
#include "portico/pr_host.h"
#include "portico/pr_zip.h"
#include "portico/pr_winhello.h"

#endif // PORTICO_RUNTIME_H
