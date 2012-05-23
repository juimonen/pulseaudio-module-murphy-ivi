#ifndef foouserdatafoo
#define foouserdatafoo

#include <stdbool.h>
#include <pulsecore/core.h>

#define PA_PROP_ROUTING_CLASS_NAME  "routing.class.name"
#define PA_PROP_ROUTING_CLASS_ID    "routing.class.id"
#define PA_PROP_ROUTING_METHOD      "routing.method"

#define PA_ROUTING_DEFAULT           "default"
#define PA_ROUTING_EXPLICIT          "explicit"

typedef struct pa_card               pa_card;
typedef struct pa_sink               pa_sink;

typedef struct pa_null_sink          pa_null_sink;
typedef struct pa_tracker            pa_tracker;
typedef struct pa_audiomgr           pa_audiomgr;
typedef struct pa_policy_dbusif      pa_policy_dbusif;
typedef struct pa_discover           pa_discover;
typedef struct pa_router             pa_router;
typedef struct pa_mir_config         pa_mir_config;
typedef struct pa_node_card          pa_node_card;
typedef struct pa_card_hooks         pa_card_hooks;
typedef struct pa_sink_hooks         pa_sink_hooks;
typedef struct pa_source_hooks       pa_source_hooks;
typedef struct pa_sink_input_hooks   pa_sink_input_hooks;

typedef enum   mir_direction         mir_direction;
typedef enum   mir_implement         mir_implement;
typedef enum   mir_location          mir_location;
typedef enum   mir_node_type         mir_node_type;
typedef enum   mir_privacy           mir_privacy; 
typedef struct mir_node              mir_node;
typedef struct mir_rtgroup           mir_rtgroup;
typedef struct mir_rtentry           mir_rtentry;

typedef struct am_domainreg_data     am_domainreg_data;
typedef struct am_nodereg_data       am_nodereg_data;
typedef struct am_nodeunreg_data     am_nodeunreg_data;
typedef struct am_ack_data           am_ack_data;
typedef struct am_connect_data       am_connect_data;



typedef struct {
    char *profile;    /**< During profile change it contains the new profile
                           name. Otherwise it is NULL. When sink tracking
                           hooks called the card's active_profile still
                           points to the old profile */
} pa_mir_state;


struct userdata {
    pa_core           *core;
    pa_module         *module;
    pa_null_sink      *nullsink;
    pa_audiomgr       *audiomgr;
    pa_policy_dbusif  *dbusif;
    pa_discover       *discover;
    pa_tracker        *tracker;
    pa_router         *router;
    pa_mir_config     *config;
    pa_mir_state       state;
};

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
