#include <stdio.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/strbuf.h>


#include "discover.h"
#include "node.h"
#include "audiomgr.h"
#include "router.h"
#include "utils.h"

#define MAX_CARD_TARGET   4
#define MAX_NAME_LENGTH   256

typedef struct {
    struct userdata *u;
    uint32_t index;
} card_check_t;


static void handle_alsa_card(struct userdata *, pa_card *);
static void handle_bluetooth_card(struct userdata *, pa_card *);

static void handle_udev_loaded_card(struct userdata *, pa_card *,
                                    mir_node *, char *);
static void handle_card_ports(struct userdata *, mir_node *,
                              pa_card *, pa_card_profile *);

static mir_node *create_node(struct userdata *, mir_node *, pa_bool_t *);
static void destroy_node(struct userdata *, mir_node *);

static void parse_profile_name(pa_card_profile *,
                               char **, char **, char *, int);

static char *node_key_from_card(struct userdata *, mir_direction,
                                void *, char *, size_t);

static void set_stream_routing_properties(pa_proplist *, mir_node_type,
                                          pa_sink *);
static mir_node_type get_stream_routing_class(pa_proplist *);



static void classify_node_by_card(mir_node *, pa_card *,
                                  pa_card_profile *, pa_device_port *);
static void guess_device_node_type_and_name(mir_node *, const char *,
                                            const char *);
static mir_node_type guess_stream_node_type(pa_proplist *);

static void schedule_deferred_routing(struct userdata *);
static void schedule_card_check(struct userdata *, pa_card *);


static void pa_hashmap_node_free(void *node, void *u)
{
    mir_node_destroy(u, node);
}


struct pa_discover *pa_discover_init(struct userdata *u)
{
    pa_discover *discover = pa_xnew0(pa_discover, 1);

    discover->chmin = 1;
    discover->chmax = 2;
    discover->selected = TRUE;

    discover->nodes.byname = pa_hashmap_new(pa_idxset_string_hash_func,
                                            pa_idxset_string_compare_func);
    discover->nodes.byptr  = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                            pa_idxset_trivial_compare_func);
    return discover;
}

void pa_discover_done(struct userdata *u)
{
    pa_discover *discover;

    if (u && (discover = u->discover)) {
        pa_hashmap_free(discover->nodes.byname, pa_hashmap_node_free,u);
        pa_hashmap_free(discover->nodes.byptr, NULL,NULL);
        pa_xfree(discover);
        u->discover = NULL;
    }
}

void pa_discover_domain_up(struct userdata *u)
{
    pa_discover *discover;
    mir_node    *node;
    void        *state;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
        node->amid = AM_ID_INVALID;

        if (node->visible && node->available)
            pa_audiomgr_register_node(u, node);
    }
}

void pa_discover_domain_down(struct userdata *u)
{
}

void pa_discover_add_card(struct userdata *u, pa_card *card)
{
    const char *bus;

    pa_assert(u);
    pa_assert(card);

    if (!(bus = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS))) {
        pa_log_debug("ignoring card '%s' due to lack of '%s' property",
                     pa_utils_get_card_name(card), PA_PROP_DEVICE_BUS);
        return;
    }

    if (pa_streq(bus, "pci") || pa_streq(bus, "usb")) {
        handle_alsa_card(u, card);
        return;
    }
    else if (pa_streq(bus, "bluetooth")) {
        handle_bluetooth_card(u, card);
        return;
    }

    pa_log_debug("ignoring card '%s' due to unsupported bus type '%s'",
                 pa_utils_get_card_name(card), bus);
}

void pa_discover_remove_card(struct userdata *u, pa_card *card)
{
    pa_discover *discover;
    mir_node    *node;
    void        *state;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((discover = u->discover));

    PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
        if (node->implement == mir_device &&
            node->pacard.index == card->index)
        {
            destroy_node(u, node);
        }
    }
}

void pa_discover_profile_changed(struct userdata *u, pa_card *card)
{
    pa_card_profile *prof;
    pa_discover     *discover;
    const char      *bus;
    pa_bool_t        pci;
    pa_bool_t        usb;
    pa_bool_t        bluetooth;
    uint32_t         stamp;
    mir_node        *node;
    void            *state;
    
    pa_assert(u);
    pa_assert(card);
    pa_assert_se((discover = u->discover));


    if ((bus = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS)) == NULL) {
        pa_log_debug("ignoring profile change on card '%s' due to lack of '%s'"
                     "property", pa_utils_get_card_name(card),
                     PA_PROP_DEVICE_BUS);
        return;
    }

    pci = pa_streq(bus, "pci");
    usb = pa_streq(bus, "usb");
    bluetooth = pa_streq(bus, "bluetooth");

    if (!pci && !usb && !bluetooth) {
        pa_log_debug("ignoring profile change on card '%s' due to unsupported "
                     "bus type '%s'", pa_utils_get_card_name(card), bus);
    }

    if (bluetooth) {
        pa_assert_se((prof = card->active_profile));

        pa_log_debug("bluetooth profile changed to '%s' on card '%s'",
                     prof->name, card->name);
        
        if (!prof->n_sinks && !prof->n_sources) {
            /* switched of but not unloaded yet */
            PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
                if (node->implement == mir_device &&
                    node->pacard.index == card->index)
                {
                    node->available = FALSE;
                }
            }
        }
    }
    else {
        pa_log_debug("alsa profile changed to '%s' on card '%s'",
                     card->active_profile->name, card->name);

        stamp = pa_utils_get_stamp();
 
        handle_alsa_card(u, card);

        PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
            if (node->implement == mir_device &&
                node->pacard.index == card->index &&
                node->stamp < stamp)
            {
                destroy_node(u, node);
            }
        }
    }

}

void pa_discover_add_sink(struct userdata *u, pa_sink *sink, pa_bool_t route)
{
    pa_discover    *discover;
    mir_node       *node;
    pa_card        *card;
    char           *key;
    mir_node_type   type;
    mir_node        data;
    char            buf[256];

    pa_assert(u);
    pa_assert(sink);
    pa_assert_se((discover = u->discover));

    if ((card = sink->card)) {
        if (!(key = node_key_from_card(u, mir_output, sink, buf, sizeof(buf))))
            return;
        if (!(node = pa_discover_find_node(u, key))) {
            pa_log_debug("can't find node for sink (key '%s')", key);
            return;
        }
        pa_log_debug("node for '%s' found (key %s). Updating with sink data",
                     node->paname, node->key);
        node->paidx = sink->index;
        pa_hashmap_put(discover->nodes.byptr, sink, node);

        type = node->type;

        if (route) {
            if (type != mir_bluetooth_a2dp && type != mir_bluetooth_sco)
                mir_router_make_routing(u);
            else {
                if (!u->state.profile)
                    schedule_deferred_routing(u);
            }
        }
    }
    else {
        memset(&data, 0, sizeof(data));
        data.key = pa_xstrdup(sink->name);
        data.direction = mir_output;
        data.implement = mir_device;
        data.channels  = sink->channel_map.channels;

        if (sink == pa_utils_get_null_sink(u)) {
            data.visible = FALSE;
            data.available = TRUE;
            data.type = mir_null;
            data.amname = pa_xstrdup("Silent");
            data.amid = AM_ID_INVALID;
            data.paname = pa_xstrdup(sink->name);
            data.paidx = sink->index;
        }
        else {
            pa_xfree(data.key); /* for now */
            pa_log_info("currently we do not support statically loaded sinks");
            return;
        }

        create_node(u, &data, NULL);
    }
}


void pa_discover_remove_sink(struct userdata *u, pa_sink *sink)
{
    pa_discover    *discover;
    mir_node       *node;
    char           *name;
    mir_node_type   type;

    pa_assert(u);
    pa_assert(sink);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_sink_name(sink);

    if (!(node = pa_hashmap_get(discover->nodes.byptr, sink)))
        pa_log_debug("can't find node for sink (name '%s')", name);
    else {
        pa_log_debug("node found for '%s'. Reseting sink data", name);
        node->paidx = PA_IDXSET_INVALID;
        pa_hashmap_remove(discover->nodes.byptr, sink);

        type = node->type;

        if (sink->card) {
            if (type != mir_bluetooth_a2dp && type != mir_bluetooth_sco)
                node->available = FALSE;
            else {
                if (!u->state.profile)
                    schedule_deferred_routing(u);
            }
        }
        else {
            pa_log_info("currently we do not support statically loaded sinks");
        }
    }
}


void pa_discover_add_source(struct userdata *u, pa_source *source)
{
    pa_discover    *discover;
    mir_node       *node;
    pa_card        *card;
    char           *key;
    char            buf[256];

    pa_assert(u);
    pa_assert(source);
    pa_assert_se((discover = u->discover));

    if ((card = source->card)) {
        if (!(key = node_key_from_card(u, mir_output,source, buf,sizeof(buf))))
            return;
        if (!(node = pa_discover_find_node(u, key))) {
            pa_log_debug("can't find node for source (key '%s')", key);
            return;
        }
        pa_log_debug("node for '%s' found. Updating with source data",
                     node->amname);
        node->paidx = source->index;
        pa_hashmap_put(discover->nodes.byptr, source, node);
    }
}


void pa_discover_remove_source(struct userdata *u, pa_source *source)
{
    pa_discover    *discover;
    mir_node       *node;
    char           *name;

    pa_assert(u);
    pa_assert(source);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_source_name(source);

    if (!(node = pa_hashmap_get(discover->nodes.byptr, source)))
        pa_log_debug("can't find node for source (name '%s')", name);
    else {
        pa_log_debug("node found. Reseting source data");
        node->paidx = PA_IDXSET_INVALID;
        pa_hashmap_remove(discover->nodes.byptr, source);
    }
}


void pa_discover_preroute_sink_input(struct userdata *u,
                                     pa_sink_input_new_data *data)
{
    pa_core       *core;
    pa_proplist   *pl;
    pa_discover   *discover;
    mir_node_type  type;
    mir_node       fake;
    mir_node      *target;
    pa_sink       *sink;
    
    pa_assert(u);
    pa_assert(data);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((pl = data->proplist));

    type = guess_stream_node_type(pl);

    set_stream_routing_properties(pl, type, data->sink);

    if (!data->sink) {
        memset(&fake, 0, sizeof(fake));
        fake.direction = mir_input;
        fake.implement = mir_stream;
        fake.channels  = data->channel_map.channels;
        fake.type      = type;
        fake.visible   = TRUE;
        fake.available = TRUE;
        fake.amname    = "<preroute>";

        target = mir_router_make_prerouting(u, &fake);

        if (!target)
            pa_log("there is no default route for the new stream");
        else if (target->paidx == PA_IDXSET_INVALID)
            pa_log("can't route to the default '%s': no sink", target->amname);
        else {
            if ((sink = pa_idxset_get_by_index(core->sinks, target->paidx))) {
                if (!pa_sink_input_new_data_set_sink(data, sink, FALSE))
                    pa_log("can't set sink %d for new sink-input",sink->index);
            }
            else {
                pa_log("can't route to the default '%s': sink lookup failed",
                       target->amname);
            }
        }
    }
}


void pa_discover_add_sink_input(struct userdata *u, pa_sink_input *sinp)
{
    pa_proplist    *pl;
    pa_discover    *discover;
    mir_node        data;
    mir_node       *node;
    mir_node       *sinknod;
    char           *name;
    mir_node_type   type;
    char            key[256];
    pa_bool_t       created;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((discover = u->discover));
    pa_assert_se((pl = sinp->proplist));

    name = pa_utils_get_sink_input_name(sinp);
    type = get_stream_routing_class(pl);

    pa_log_debug("dealing with new stream '%s'", name);

    if (type == mir_node_type_unknown) {
        if ((type = guess_stream_node_type(pl)) == mir_node_type_unknown) {
            pa_log_debug("cant find stream class for '%s'. "
                         "Leaving it alone", name);
            return;
        }

        set_stream_routing_properties(pl, type, NULL);

        /* make some post-routing here */
    }

    /* we need to add this to main hashmap as that is used for loop
       through on all nodes. */
    snprintf(key, sizeof(key), "stream_input.%d", sinp->index);

    memset(&data, 0, sizeof(data));
    data.key       = key;
    data.direction = mir_input;
    data.implement = mir_stream;
    data.channels  = sinp->channel_map.channels;
    data.type      = type;
    data.visible   = true;
    data.available = true;
    data.amname    = name;
    data.amdescr   = (char *)pa_proplist_gets(pl, PA_PROP_MEDIA_NAME);
    data.amid      = AM_ID_INVALID;
    data.paname    = name;
    data.paidx     = sinp->index;

    node = create_node(u, &data, &created);

    pa_assert(node);

    if (!created) {
        pa_log("%s: confused with stream. '%s' did exists",
               __FILE__, node->amname);
    }
    else {
        pa_hashmap_put(discover->nodes.byptr, sinp, node);

        if (!(sinknod = pa_hashmap_get(discover->nodes.byptr, sinp->sink)))
            pa_log_debug("can't figure out where this stream is routed");
        else {
            pa_log_debug("register route '%s' => '%s'",
                         node->amname, sinknod->amname);
            /* FIXME: and actually do it ... */
        }
    }
}


void pa_discover_remove_sink_input(struct userdata *u, pa_sink_input *sinp)
{
    pa_discover    *discover;
    mir_node       *node;
    mir_node       *sinknod;
    char           *name;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_sink_input_name(sinp);

    if (!(node = pa_hashmap_remove(discover->nodes.byptr, sinp)))
        pa_log_debug("can't find node for sink-input (name '%s')", name);
    else {
        pa_log_debug("node found for '%s'. After clearing the route "
                     "it will be destroyed", name);

        if (!(sinknod = pa_hashmap_get(discover->nodes.byptr, sinp->sink)))
            pa_log_debug("can't figure out where this stream is routed");
        else {
            pa_log_debug("clear route '%s' => '%s'",
                         node->amname, sinknod->amname);
            /* FIXME: and actually do it ... */
        }

        destroy_node(u, node);
    }
}


mir_node *pa_discover_find_node(struct userdata *u, const char *key)
{
    pa_discover *discover;
    mir_node    *node;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    if (key)
        node = pa_hashmap_get(discover->nodes.byname, key);
    else
        node = NULL;

    return node;
}


static void handle_alsa_card(struct userdata *u, pa_card *card)
{
    mir_node         data;
    const char      *udd;
    char            *cnam;
    char            *cid;

    memset(&data, 0, sizeof(data));
    data.visible = TRUE;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;
    data.stamp = pa_utils_get_stamp();

    cnam = pa_utils_get_card_name(card);
    udd  = pa_proplist_gets(card->proplist, "module-udev-detect.discovered");

    if (udd && pa_streq(udd, "1")) {
        /* udev loaded alsa card */
        if (!strncmp(cnam, "alsa_card.", 10)) {
            cid = cnam + 10;
            handle_udev_loaded_card(u, card, &data, cid);
            return;
        }
    }
    else {
        /* statically loaded pci or usb card */
    }

    pa_log_debug("ignoring unrecognized pci card '%s'", cnam);
}


static void handle_bluetooth_card(struct userdata *u, pa_card *card)
{
    pa_discover     *discover;
    pa_card_profile *prof;
    mir_node         data;
    char            *cnam;
    char            *cid;
    const char      *cdescr;
    void            *state;
    char             paname[MAX_NAME_LENGTH+1];
    char             amname[MAX_NAME_LENGTH+1];
    char             key[MAX_NAME_LENGTH+1];
    

    pa_assert_se((discover = u->discover));

    cdescr = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);


    memset(paname, 0, sizeof(paname));
    memset(amname, 0, sizeof(amname));
    memset(key   , 0, sizeof(key)   );

    memset(&data, 0, sizeof(data));
    data.key = key;
    data.visible = TRUE;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;
    data.paname = paname;
    data.amname = amname;
    data.amdescr = (char *)cdescr;
    data.pacard.index = card->index;
    data.stamp = pa_utils_get_stamp();

    cnam = pa_utils_get_card_name(card);

    if (!strncmp(cnam, "bluez_card.", 11)) { 
        cid = cnam + 11;

        PA_HASHMAP_FOREACH(prof, card->profiles, state) {
            data.available = TRUE;
            data.pacard.profile = prof->name;

            if (prof->n_sinks > 0) {
                data.direction = mir_output;
                data.channels = prof->max_sink_channels; 
                data.amname = amname;
                amname[0] = '\0';
                snprintf(paname, sizeof(paname), "bluez_sink.%s", cid);
                snprintf(key, sizeof(key), "%s@%s", paname, prof->name);
                classify_node_by_card(&data, card, prof, NULL);
                create_node(u, &data, NULL);
            }

            if (prof->n_sources > 0) {
                data.direction = mir_input;
                data.channels = prof->max_source_channels; 
                data.amname = amname;
                amname[0] = '\0';
                snprintf(paname, sizeof(paname), "bluez_source.%s", cid);
                snprintf(key, sizeof(key), "%s@%s", paname, prof->name);
                classify_node_by_card(&data, card, prof, NULL);
                create_node(u, &data, NULL);
            }
        }

        if (!(prof = card->active_profile))
            pa_log("card '%s' has no active profile", card->name);
        else {
            pa_log_debug("card '%s' default profile '%s'",
                         card->name, prof->name);
        }

        schedule_card_check(u, card);
    }
}


static void handle_udev_loaded_card(struct userdata *u, pa_card *card,
                                    mir_node *data, char *cardid)
{
    pa_discover      *discover;
    pa_card_profile  *prof;
    pa_card_profile  *active;
    void             *state;
    const char       *alsanam;
    char             *sid;
    char             *sinks[MAX_CARD_TARGET+1];
    char             *sources[MAX_CARD_TARGET+1];
    char              buf[MAX_NAME_LENGTH+1];
    char              paname[MAX_NAME_LENGTH+1];
    char              amname[MAX_NAME_LENGTH+1];
    int               i;

    pa_assert(card);
    pa_assert(card->profiles);
    pa_assert_se((discover = u->discover));

    alsanam = pa_proplist_gets(card->proplist, "alsa.card_name");

    memset(amname, 0, sizeof(amname));

    data->paname  = paname;
    data->amname  = amname;
    data->amdescr = (char *)alsanam;

    data->pacard.index = card->index;

    active = card->active_profile;

    PA_HASHMAP_FOREACH(prof, card->profiles, state) {
        /* filtering: deal with sepected profiles if requested so */
        if (discover->selected && (!active || (active && prof != active)))
            continue;

        /* filtering: skip the 'off' profiles */
        if (!prof->n_sinks && !prof->n_sources)
            continue;

        /* filtering: consider sinks with suitable amount channels */
        if (prof->n_sinks && 
            (prof->max_sink_channels < discover->chmin ||
             prof->max_sink_channels  > discover->chmax  ))
            continue;
        
        /* filtering: consider sources with suitable amount channels */
        if (prof->n_sources &&
            (prof->max_source_channels <  discover->chmin ||
             prof->max_source_channels >  discover->chmax   ))
            continue;

        data->pacard.profile = prof->name;

        parse_profile_name(prof, sinks,sources, buf,sizeof(buf));
        
        data->direction = mir_output;
        data->channels = prof->max_sink_channels;
        for (i = 0;  (sid = sinks[i]);  i++) {
            snprintf(paname, sizeof(paname), "alsa_output.%s.%s", cardid, sid);
            handle_card_ports(u, data, card, prof);
        }

        data->direction = mir_input;
        data->channels = prof->max_source_channels;
        for (i = 0;  (sid = sources[i]);  i++) {
            snprintf(paname, sizeof(paname), "alsa_input.%s.%s", cardid, sid);
            handle_card_ports(u, data, card, prof);
        }        
    }
}


static void handle_card_ports(struct userdata *u, mir_node *data,
                              pa_card *card, pa_card_profile *prof)
{
    mir_node       *node = NULL;
    pa_bool_t       have_ports = FALSE;
    char           *amname = data->amname;
    pa_device_port *port;
    void           *state;
    pa_bool_t       created;
    char            key[MAX_NAME_LENGTH+1];

    pa_assert(u);
    pa_assert(data);
    pa_assert(card);
    pa_assert(prof);

    if (card->ports) {        
        PA_HASHMAP_FOREACH(port, card->ports, state) {
            /*
             * if this port did not belong to any profile 
             * (ie. prof->profiles == NULL) we assume that this port
             * does works with all the profiles
             */
            if (port->profiles && pa_hashmap_get(port->profiles, prof->name) &&
                ((port->is_input && data->direction == mir_input)||
                 (port->is_output && data->direction == mir_output)))
            {
                have_ports = TRUE;

                /* make constrain if node != NULL and add node to it */

                amname[0] = '\0';
                snprintf(key, sizeof(key), "%s@%s", data->paname, port->name);

                data->key       = key;
                data->available = (port->available != PA_PORT_AVAILABLE_NO);
                data->type      = 0;
                data->amname    = amname;
                data->paport    = port->name;

                classify_node_by_card(data, card, prof, port);

                node = create_node(u, data, &created);

                if (created)
                    ; /* if constrain != NULL add the node to it */
                else {
                    node->stamp = data->stamp;
                }
            }
        }
    }
    
    if (!have_ports) {
        data->key = data->paname;
        data->available = TRUE;

        classify_node_by_card(data, card, prof, NULL);

        node = create_node(u, data, &created);

        if (!created)
            node->stamp = data->stamp;
    }

    data->amname = amname;
    *amname = '\0';
}


static mir_node *create_node(struct userdata *u, mir_node *data, 
                             pa_bool_t *created_ret)
{
    pa_discover *discover;
    mir_node    *node;
    pa_bool_t    created;
    char         buf[2048];

    pa_assert(u);
    pa_assert(data);
    pa_assert(data->key);
    pa_assert(data->paname);
    pa_assert_se((discover = u->discover));
    
    if ((node = pa_hashmap_get(discover->nodes.byname, data->key)))
        created = FALSE;
    else {
        created = TRUE;
        
        node = mir_node_create(u, data);
        pa_hashmap_put(discover->nodes.byname, node->key, node);
        
        mir_node_print(node, buf, sizeof(buf));
        pa_log_debug("new node:\n%s", buf);
        
        pa_audiomgr_register_node(u, node);
    }
    
    if (created_ret)
        *created_ret = created;
    
    return node;
}

static void destroy_node(struct userdata *u, mir_node *node)
{
    pa_discover *discover;
    mir_node    *removed;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    if (node) {
        removed = pa_hashmap_remove(discover->nodes.byname, node->key);

        if (node != removed) {
            if (removed)
                pa_log("%s: confused with data structures: key mismatch. "
                       " attempted to destroy '%s'; actually destroyed '%s'",
                       __FILE__, node->key, removed->key);
            else
                pa_log("%s: confused with data structures: node '%s' "
                       "is not in the hash table", __FILE__, node->key);
            return;
        }

        pa_log_debug("destroying node: %s / '%s'", node->key, node->amname);
        
        pa_audiomgr_unregister_node(u, node);
        
        mir_node_destroy(u, node);
    }    
}

static char *get_name(char **string_ptr, int offs)
{
    char c, *name, *end;

    name = *string_ptr + offs;

    for (end = name;  (c = *end);   end++) {
        if (c == '+') {
            *end++ = '\0';
            break;
        }
    }

    *string_ptr = end;

    return name;
} 

static void parse_profile_name(pa_card_profile *prof,
                               char           **sinks,
                               char           **sources,
                               char            *buf,
                               int              buflen)
{
    char *p = buf;
    int   i = 0;
    int   j = 0;

    pa_assert(prof->name);

    strncpy(buf, prof->name, buflen);
    buf[buflen-1] = '\0';

    memset(sinks, 0, sizeof(char *) * (MAX_CARD_TARGET+1));
    memset(sources, 0, sizeof(char *) * (MAX_CARD_TARGET+1));

    do {
        if (!strncmp(p, "output:", 7)) {
            if (i >= MAX_CARD_TARGET) {
                pa_log_debug("number of outputs exeeds the maximum %d in "
                             "profile name '%s'", MAX_CARD_TARGET, prof->name);
                return;
            }
            sinks[i++] = get_name(&p, 7);
        } 
        else if (!strncmp(p, "input:", 6)) {
            if (j >= MAX_CARD_TARGET) {
                pa_log_debug("number of inputs exeeds the maximum %d in "
                             "profile name '%s'", MAX_CARD_TARGET, prof->name);
                return;
            }
            sources[j++] = get_name(&p, 6);            
        }
        else {
            pa_log("%s: failed to parse profile name '%s'",
                   __FILE__, prof->name);
            return;
        }
    } while (*p);
}


static char *node_key_from_card(struct userdata *u, mir_direction direction,
                                void *data, char *buf, size_t len)
{
    pa_card         *card;
    pa_card_profile *profile;
    pa_device_port  *port;
    const char      *bus;
    pa_bool_t        pci;
    pa_bool_t        usb;
    pa_bool_t        bluetooth;
    char            *type;
    char            *name;
    const char      *profile_name;
    char            *key;

    pa_assert(u);
    pa_assert(data);
    pa_assert(direction == mir_input || direction == mir_output);

    if (direction == mir_input) {
        pa_sink *sink = data;
        type  = "sink";
        name  = pa_utils_get_sink_name(sink);
        card  = sink->card;
        port  = sink->active_port;
    }
    else {
        pa_source *source = data;
        type = "source";
        name = pa_utils_get_source_name(source);
        card = source->card;
        port = source->active_port;
    }

    if (!card)
        return NULL;
        
    pa_assert_se((profile = card->active_profile));

    if (!u->state.profile)
        profile_name = profile->name;
    else {
        pa_log_debug("state.profile is not null. '%s' supresses '%s'",
                     u->state.profile, profile->name);
        profile_name = u->state.profile;
    }
        

    if (!(bus = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS))) {
        pa_log_debug("ignoring %s '%s' due to lack of '%s' property "
                     "on its card", type, name, PA_PROP_DEVICE_BUS);
        return NULL;
    }
    
    pci = pa_streq(bus, "pci");
    usb = pa_streq(bus, "usb");
    bluetooth = pa_streq(bus, "bluetooth");
    
    if (!pci && !usb && !bluetooth) {
        pa_log_debug("ignoring %s '%s' due to unsupported bus type '%s' "
                     "of its card", type, name, bus);
        return NULL;
    }
    
    if (bluetooth) {
        key = buf;
        snprintf(buf, len, "%s@%s", name, profile_name);
    }
    else {
        if (!port)
            key = name;
        else {
            key = buf;
            snprintf(buf, len, "%s@%s", name, port->name);
        }
    }

    return key;
}

static void set_stream_routing_properties(pa_proplist   *pl,
                                          mir_node_type  styp,
                                          pa_sink       *sink)
{
    const char    *clnam;
    const char    *method;
    char           clid[32];

    pa_assert(pl);
    pa_assert(styp);
    
    snprintf(clid, sizeof(clid), "%d", styp);
    clnam  = mir_node_type_str(styp);
    method = sink ? PA_ROUTING_EXPLICIT : PA_ROUTING_DEFAULT;

    if (pa_proplist_sets(pl, PA_PROP_ROUTING_CLASS_NAME, clnam ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_ROUTING_CLASS_ID  , clid  ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_ROUTING_METHOD    , method) < 0  )
    {
        pa_log("failed to set properties on sink-input. "
               "some routing function might malfunction later on");
    }
}

static mir_node_type get_stream_routing_class(pa_proplist *pl)
{
    const char    *clid;
    mir_node_type  type;
    char          *e;

    pa_assert(pl);

    if ((clid = pa_proplist_gets(pl, PA_PROP_ROUTING_CLASS_ID))) {
        type = strtol(clid, &e, 10);

        if (!*e) {
            if (type >= mir_application_class_begin &&
                type <  mir_application_class_end)
            {
                return type;
            }
        }                
    }

    return mir_node_type_unknown;
}


static void classify_node_by_card(mir_node *data, pa_card *card,
                                  pa_card_profile *prof, pa_device_port *port)
{
    const char *bus;
    const char *form;
    
    pa_assert(data);
    pa_assert(card);

    bus  = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS);
    form = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_FORM_FACTOR);

    if (form) {
        if (!strcasecmp(form, "internal")) {
            data->location = mir_external;
            if (port && !strcasecmp(bus, "pci")) {
                guess_device_node_type_and_name(data, port->name,
                                                port->description);
            }
        }
        else if (!strcasecmp(form, "speaker") || !strcasecmp(form, "car")) {
            if (data->direction == mir_output) {
                data->location = mir_internal;
                data->type = mir_speakers;
            }
        }
        else if (!strcasecmp(form, "handset")) {
            data->location = mir_external;
            data->type = mir_phone;
            data->privacy = mir_private;
        }
        else if (!strcasecmp(form, "headset")) {
            data->location = mir_external;
            if (bus) {
                if (!strcasecmp(bus,"usb")) {
                    data->type = mir_usb_headset;
                }
                else if (!strcasecmp(bus,"bluetooth")) {
                    if (prof && !strcmp(prof->name, "a2dp"))
                        data->type = mir_bluetooth_a2dp;
                    else 
                        data->type = mir_bluetooth_sco;
                }
                else {
                    data->type = mir_wired_headset;
                }
            }
        }
        else if (!strcasecmp(form, "headphone")) {
            if (data->direction == mir_output) {
                data->location = mir_external;
                if (bus) {
                    if (!strcasecmp(bus,"usb"))
                        data->type = mir_usb_headphone;
                    else if (strcasecmp(bus,"bluetooth"))
                        data->type = mir_wired_headphone;
                }
            }
        }
        else if (!strcasecmp(form, "microphone")) {
            if (data->direction == mir_input) {
                data->location = mir_external;
                data->type = mir_microphone;
            }
        }
    }
    else {
        if (port && !strcasecmp(bus, "pci")) {
            guess_device_node_type_and_name(data, port->name,
                                            port->description);
        }
    }

    if (!data->amname[0]) {
        if (data->type != mir_node_type_unknown)
            data->amname = (char *)mir_node_type_str(data->type);
        else if (port && port->description)
            data->amname = port->description;
        else if (port && port->name)
            data->amname = port->name;
        else
            data->amname = data->paname;
    }


    if (data->direction == mir_input)
        data->privacy = mir_privacy_unknown;
    else {
        switch (data->type) {
            /* public */
        default:
        case mir_speakers:
        case mir_front_speakers:
        case mir_rear_speakers:
            data->privacy = mir_public;
            break;
            
            /* private */
        case mir_phone:
        case mir_wired_headset:
        case mir_wired_headphone:
        case mir_usb_headset:
        case mir_usb_headphone:
        case mir_bluetooth_sco:
        case mir_bluetooth_a2dp:
            data->privacy = mir_private;
            break;
            
            /* unknown */
        case mir_null:
        case mir_jack:
        case mir_spdif:
        case mir_hdmi:
            data->privacy = mir_privacy_unknown;
            break;
        } /* switch */
    }
}


/* data->direction must be set */
static void guess_device_node_type_and_name(mir_node *data,
                                            const char *pnam,
                                            const char *pdesc)
{
    if (data->direction == mir_output && strcasestr(pnam, "headphone")) {
        data->type = mir_wired_headphone;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "headset")) {
        data->type = mir_wired_headset;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "line")) {
        data->type = mir_jack;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "spdif")) {
        data->type = mir_spdif;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "hdmi")) {
        data->type = mir_hdmi;
        data->amname = (char *)pdesc;
    }
    else if (data->direction == mir_input && strcasestr(pnam, "microphone")) {
        data->type = mir_microphone;
        data->amname = (char *)pdesc;
    }
    else if (data->direction == mir_output && strcasestr(pnam,"analog-output"))
        data->type = mir_speakers;
    else if (data->direction == mir_input && strcasestr(pnam, "analog-input"))
        data->type = mir_jack;
    else {
        data->type = mir_node_type_unknown;
    }
}

static mir_node_type guess_stream_node_type(pa_proplist *pl)
{
    typedef struct {
        char *id;
        mir_node_type type;
    } map_t;

    static map_t role_map[] = {
        {"video"    , mir_player },
        {"music"    , mir_player },
        {"game"     , mir_game   },
        {"event"    , mir_event  },
        {"phone"    , mir_player },
        {"animation", mir_browser},
        {"test"     , mir_player },
        {NULL, mir_node_type_unknown}
    };

    static map_t bin_map[] = {
        {"rhytmbox"    , mir_player },
        {"firefox"     , mir_browser},
        {"chrome"      , mir_browser},
        {"sound-juicer", mir_player },
        {NULL, mir_node_type_unknown}
    };


    mir_node_type  rtype, btype;
    const char    *role;
    const char    *bin;
    map_t         *m;

    pa_assert(pl);

    rtype = btype = mir_node_type_unknown;

    role = pa_proplist_gets(pl, PA_PROP_MEDIA_ROLE);
    bin  = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY);

    if (role) {
        for (m = role_map;  m->id;  m++) {
            if (pa_streq(role, m->id))
                break;
        }
        rtype = m->type;
    }

    if (rtype != mir_node_type_unknown && rtype != mir_player)
        return rtype;

    if (bin) {
        for (m = bin_map;  m->id;  m++) {
            if (pa_streq(bin, m->id))
                break;
        }
        btype = m->type;
    }

    if (btype == mir_node_type_unknown)
        return rtype;

    return btype;
}


static void deferred_routing_cb(pa_mainloop_api *m, void *d)
{
    struct userdata *u = d;

    (void)m;

    pa_assert(u);

    pa_log_debug("deferred routing starts");

    mir_router_make_routing(u);
}


static void schedule_deferred_routing(struct userdata *u)
{
    pa_core *core;
    pa_mainloop_api *a;

    pa_assert(u);
    pa_assert_se((core = u->core));

    pa_log_debug("scheduling deferred routing");

    pa_mainloop_api_once(core->mainloop, deferred_routing_cb, u);
}


static void card_check_cb(pa_mainloop_api *m, void *d)
{
    card_check_t *cc = d;
    struct userdata *u;
    pa_core *core;
    pa_card *card;
    pa_sink *sink;
    pa_source *source;
    pa_discover *discover;
    int n_sink, n_source;
    uint32_t idx;

    (void)m;

    pa_assert(cc);
    pa_assert((u = cc->u));
    pa_assert((core = u->core));

    pa_log_debug("card check starts");

    if (!(card = pa_idxset_get_by_index(core->cards, cc->index)))
        pa_log_debug("card %u is gone", cc->index);
    else {
        n_sink = n_source = 0;

        PA_IDXSET_FOREACH(sink, core->sinks, idx) {
            if ((sink->card) && sink->card->index == card->index)
                n_sink++;
        }

        PA_IDXSET_FOREACH(source, core->sources, idx) {
            if ((source->card) && source->card->index == card->index)
                n_sink++;
        }

        if (n_sink || n_source) {
            pa_log_debug("found %u sinks and %u sources belonging to "
                         "'%s' card", n_sink, n_source, card->name);
            pa_log_debug("nothing to do");
        }
        else {
            pa_log_debug("card '%s' has no sinks/sources. Do routing ...",
                         card->name);
            mir_router_make_routing(u);
        }
    }
    
    pa_xfree(cc);
}


static void schedule_card_check(struct userdata *u, pa_card *card)
{
    pa_core *core;
    card_check_t *cc;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((core = u->core));

    pa_log_debug("scheduling card check");

    cc = pa_xnew0(card_check_t, 1);
    cc->u = u;
    cc->index = card->index;

    pa_mainloop_api_once(core->mainloop, card_check_cb, cc);
}

                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */