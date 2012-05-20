#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#include <pulsecore/idxset.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>

#include "module-genivi-mir-symdef.h"
#include "userdata.h"
#include "audiomgr.h"
#include "dbusif.h"
#include "discover.h"
#include "tracker.h"


PA_MODULE_AUTHOR("Janos Kovacs");
PA_MODULE_DESCRIPTION("GenIVI and Murphy compliant audio policy module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE(
    "config_dir=<configuration directory>"
    "config_file=<policy configuration file> "
    "dbus_if_name=<policy dbus interface> "
    "dbus_murphy_path=<policy daemon's path> "
    "dbus_murphy_name=<policy daemon's name> "
    "dbus_audiomgr_path=<GenIVI audio manager's path> " 
    "dbus_audiomgr_name=<GenIVI audio manager's name> " 
    "null_sink_name=<name of the null sink> "
);

static const char* const valid_modargs[] = {
    "config_dir",
    "config_file",
    "dbus_if_name",
    "dbus_murphy_path",
    "dbus_murphy_name",
    "dbus_audiomgr_path",
    "dbus_audiomgr_name",
    "null_sink_name",
    NULL
};


int pa__init(pa_module *m) {
    struct userdata *u = NULL;
    pa_modargs      *ma = NULL;
    const char      *cfgdir;
    const char      *cfgfile;
    const char      *ifnam;
    const char      *mrppath;
    const char      *mrpnam;
    const char      *ampath;
    const char      *amnam;
    const char      *nsnam;
    
    pa_assert(m);
    
    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    cfgdir  = pa_modargs_get_value(ma, "config_dir", NULL);
    cfgfile = pa_modargs_get_value(ma, "config_file", NULL);
    ifnam   = pa_modargs_get_value(ma, "dbus_if_name", NULL);
    mrppath = pa_modargs_get_value(ma, "dbus_murphy_path", NULL);
    mrpnam  = pa_modargs_get_value(ma, "dbus_murphy_name", NULL);
    ampath  = pa_modargs_get_value(ma, "dbus_audiomgr_path", NULL);
    amnam   = pa_modargs_get_value(ma, "dbus_audiomgr_name", NULL);
    nsnam   = pa_modargs_get_value(ma, "null_sink_name", NULL);
    
    u = pa_xnew0(struct userdata, 1);
    u->core     = m->core;
    u->module   = m;
    //u->nullsink = pa_sink_ext_init_null_sink(nsnam);
    u->audiomgr = pa_audiomgr_init(u);
    u->dbusif   = pa_policy_dbusif_init(u,ifnam, mrppath,mrpnam, ampath,amnam);
    u->discover = pa_discover_init(u);
    u->tracker  = pa_tracker_init(u);

    if (/*u->nullsink == NULL ||*/ u->dbusif == NULL  ||
        u->audiomgr == NULL || u->discover == NULL)
        goto fail;

    m->userdata = u;

    pa_tracker_synchronize(u);
    
    pa_modargs_free(ma);
    
    return 0;
    
 fail:
    
    if (ma)
        pa_modargs_free(ma);
    
    pa__done(m);
    
    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    
    if ((u = m->userdata)) {
    
        pa_tracker_done(u);
        pa_discover_done(u);
        pa_audiomgr_done(u);
        pa_policy_dbusif_done(u);

        //pa_sink_ext_null_sink_free(u->nullsink);

        pa_xfree(u);
    }
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */


