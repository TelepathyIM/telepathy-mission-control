#ifndef __MC_EXTENSIONS_H__
#define __MC_EXTENSIONS_H__

#include <glib-object.h>
#include <telepathy-glib/proxy.h>

#include "extensions/_gen/enums.h"
#include "extensions/_gen/cli-nmc4.h"
#include "extensions/_gen/cli-account.h"
#include "extensions/_gen/cli-account-manager.h"
#include "extensions/_gen/svc-nmc4.h"
#include "extensions/_gen/svc-account.h"
#include "extensions/_gen/svc-account-manager.h"

G_BEGIN_DECLS

#include "extensions/_gen/gtypes.h"
#include "extensions/_gen/interfaces.h"

void mc_cli_init (void);

G_END_DECLS

#endif
