#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>
#include <pulsecore/thread.h>
#include <pulsecore/strlist.h>
#include <pulsecore/time-smoother.h>
#include <pulsecore/sink.h>
#include <pulsecore/sink-input.h>

#include <combine/userdata.h>

#include "multiplex.h"
#include "utils.h"

#ifndef DEFAULT_RESAMPLER
#define DEFAULT_RESAMPLER "speex-float-3"
#endif


pa_multiplex *pa_multiplex_init(void)
{
    pa_multiplex *multiplex = pa_xnew0(pa_multiplex, 1);

    return multiplex;
}


void pa_multiplex_done(pa_multiplex *multiplex, pa_core *core)
{
    pa_muxnode *mux, *n;

    PA_LLIST_FOREACH_SAFE(mux,n, multiplex->muxnodes) {
        pa_module_unload_by_index(core, mux->module_index, FALSE);
    }
}



pa_muxnode *pa_multiplex_create(pa_multiplex   *multiplex,
                                pa_core        *core,
                                uint32_t        sink_index,
                                pa_channel_map *chmap,
                                const char     *resampler,
                                int             type)
{
    static char *modnam = "module-combine-sink";

    struct userdata *u;         /* combine's userdata! */
    struct output   *o;
    pa_muxnode      *mux;
    pa_sink         *sink;
    pa_sink_input   *sinp;
    pa_module       *module;
    char             args[512];
    uint32_t         idx;

    pa_assert(core);

    if (!resampler)
        resampler = DEFAULT_RESAMPLER;

    if (!(sink = pa_idxset_get_by_index(core->sinks, sink_index))) {
        pa_log_debug("can't find the primary sink (index %u) for multiplexer",
                     sink_index);
        return NULL;
    }

    snprintf(args, sizeof(args), "slaves=\"%s\" resample_method=\"%s\" "
             "channels=%u", sink->name, resampler, chmap->channels);

    if (!(module = pa_module_load(core, modnam, args))) {
        pa_log("failed to load module '%s %s'. can't multiplex", modnam, args);
        return NULL;
    }

    pa_assert_se((u = module->userdata));
    pa_assert(u->sink);

    mux = pa_xnew0(pa_muxnode, 1);
    mux->module_index = module->index;
    mux->sink_index = u->sink->index;
    mux->defstream_index = PA_IDXSET_INVALID;

    PA_LLIST_PREPEND(pa_muxnode, multiplex->muxnodes, mux);

    if (!(o = pa_idxset_first(u->outputs, &idx)))
        pa_log("can't find default multiplexer stream");
    else {
        if ((sinp = o->sink_input)) {
            pa_utils_set_stream_routing_properties(sinp->proplist, type, NULL);
            mux->defstream_index = sinp->index;
            /* Hmm... */
            sinp->flags &= ~(pa_sink_input_flags_t)PA_SINK_INPUT_DONT_MOVE;
        }
    }

    pa_log_debug("multiplexer succesfully loaded");

    return mux;
}


void pa_multiplex_destroy(pa_multiplex *multiplex,
                          pa_core      *core,
                          pa_muxnode   *mux)
{
    pa_assert(multiplex);
    pa_assert(core);

    if (mux) {
        PA_LLIST_REMOVE(pa_muxnode, multiplex->muxnodes, mux);
        pa_module_unload_by_index(core, mux->module_index, FALSE);
        pa_xfree(mux);
    }
}


pa_muxnode *pa_multiplex_find(pa_multiplex *multiplex, uint32_t sink_index)
{
    pa_muxnode *mux;

    PA_LLIST_FOREACH(mux, multiplex->muxnodes) {
        if (sink_index == mux->sink_index) {
            pa_log_debug("muxnode found for sink %u", sink_index);
            return mux;
        }
    }

    pa_log_debug("can't find muxnode for sink %u", sink_index);

    return NULL;
}


pa_bool_t pa_multiplex_remove_default_route(pa_core *core,
                                            pa_muxnode *mux,
                                            pa_bool_t transfer_to_explicit)
{
    pa_module *module;
    pa_sink_input *sinp;
    uint32_t idx;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log("module %u is gone", mux->module_index);
    else if ((idx = mux->defstream_index) == PA_IDXSET_INVALID)
        pa_log_debug("mux %u do not have default stream", mux->module_index);
    else if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, idx)))
        pa_log("can't remove default route: sink-input %u is gone", idx);
    else {
        pa_assert_se((u = module->userdata));
        mux->defstream_index = PA_IDXSET_INVALID;

        if (transfer_to_explicit) {
            pa_log_debug("converting default route sink-input.%u -> sink.%u "
                         "to explicit", sinp->index, sinp->sink->index);
            pa_utils_set_stream_routing_method_property(sinp->proplist, TRUE);
            return TRUE;
        }
        else {
            /* call Jaska's routine */
        }
    }

    return FALSE;
}


pa_bool_t pa_multiplex_add_explicit_route(pa_core    *core,
                                          pa_muxnode *mux,
                                          pa_sink    *sink,
                                          int         type)
{
    pa_module *module;
    pa_sink_input *sinp;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log("module %u is gone", mux->module_index);
    else {
        pa_assert_se((u = module->userdata));

        if (sink == u->sink) {
            pa_log("%s: mux %d refuses to make a loopback to itself",
                   __FILE__, mux->module_index);
        }
        else {
            pa_log_debug("adding explicit route to mux %u", mux->module_index);
            /* pa_utils_set_stream_routing_properties(sinp->proplist, type, sink); */
            return TRUE;
        }
    }

    return FALSE;
}


pa_bool_t pa_multiplex_duplicate_route(pa_core       *core,
                                       pa_muxnode    *mux,
                                       pa_sink_input *sinp,
                                       pa_sink       *sink)
{
    pa_module       *module;
    struct userdata *u;   /* combine's userdata! */
    struct output   *o;
    uint32_t         idx;
    pa_sink_input   *i;

    pa_assert(core);
    pa_assert(mux);
    pa_assert(sink);

    pa_log_debug("check for duplicate route on mux %u",
                 mux->module_index);

    if (!(module = pa_idxset_get_by_index(core->modules,mux->module_index)))
        pa_log("module %u is gone", mux->module_index);
    else {
        pa_assert_se((u = module->userdata));

        PA_IDXSET_FOREACH(o, u->outputs, idx) {
            if (!(i = o->sink_input))
                continue;
            if (sinp && i == sinp)
                continue;
            if (i->sink == sink) {
                pa_log_debug("route sink-input.%u -> sink.%u is a duplicate",
                             i->index, sink->index);
                return TRUE;
            }
        }

        if (!sinp)
            pa_log_debug("no duplicate route found to sink.%u", sink->index);
        else {
            pa_log_debug("no duplicate found for route sink-input.%u -> "
                         "sink.%u", sinp->index, sink->index);
        }
    }
        
    return FALSE;
}


int pa_multiplex_print(pa_muxnode *mux, char *buf, int len)
{
    char *p, *e;

    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;

    if (!mux)
        p += snprintf(p, e-p, "<not set>");
    else {
        p += snprintf(p, e-p, "module %u, sink %u, default stream %u",
                      mux->module_index, mux->sink_index,mux->defstream_index);
    }
    
    return p - buf;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

