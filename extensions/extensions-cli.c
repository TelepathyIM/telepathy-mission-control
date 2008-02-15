#include "extensions.h"

#include <telepathy-glib/proxy.h>
#include <telepathy-glib/proxy-subclass.h>

#include "_gen/signals-marshal.h"

/* include auto-generated stubs for client-specific code */
#include "_gen/cli-nmc4-body.h"
#include "_gen/cli-account-body.h"
#include "_gen/cli-account-manager-body.h"
#include "_gen/register-dbus-glib-marshallers-body.h"

void
mc_cli_init (void)
{
  _mc_ext_register_dbus_glib_marshallers ();

  tp_proxy_or_subclass_hook_on_interface_add (TP_TYPE_PROXY,
      mc_cli_account_add_signals);

  tp_proxy_or_subclass_hook_on_interface_add (TP_TYPE_PROXY,
      mc_cli_nmc4_add_signals);

  tp_proxy_or_subclass_hook_on_interface_add (TP_TYPE_PROXY,
      mc_cli_account_manager_add_signals);
}
