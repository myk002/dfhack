#include "Core.h"
#include "Debug.h"
#include "LuaTools.h"
#include "PluginManager.h"

#include "modules/World.h"

#include "df/report.h"
#include "df/announcement_alert_type.h"
#include "df/announcement_type.h"
#include "df/world.h"

#include <unordered_map>

using std::deque;
using std::string;
using std::unordered_map;
using std::vector;

using namespace DFHack;

DFHACK_PLUGIN("announcements");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(world);

namespace DFHack {
    DBG_DECLARE(announcements, control, DebugCategory::LINFO);
    DBG_DECLARE(announcements, cycle, DebugCategory::LINFO);
}

static const string CONFIG_KEY = string(plugin_name) + "/config";
static PersistentDataItem config;

enum ConfigValues {
    CONFIG_IS_ENABLED = 0,
};

// should be small enough such that the number of reports between cycles is less
// than 3000 - MAX_TOTAL_ANNOUNCEMENTS
static const int32_t CYCLE_TICKS = 11;

// periodically refresh our bucket contents to make sure we stay in sync -- just in
// case something else has modified the reports vector
static const int32_t REFRESH_CYCLE_TICKS = 19937;

// world->frame_counter timestamps of last successful cycle
static int32_t cycle_timestamp = 0;
static int32_t refresh_cycle_timestamp = 0;

struct AnnouncementBucket {
    size_t reserved_size;
    deque<df::report> elems;
};

// max reserved reports, cumulative across all buckets
static const size_t MAX_TOTAL_RESERVED = 2000;

// DF vector is 3000 elements. this should be smaller than that by the number of reports
// we expect to see in one cycle check (see CYCLE_TICKS). if this number is too close to
// 3000, the DF vector can exceed the 3000 length limit and DF will start evicting
// elements, which will likely be our reserved elements. we will then restore those
// elements and cause churn.
static const size_t MAX_TOTAL_ANNOUNCEMENTS = 2500;

// contains copies of reserved reports so we can reinstate them if necessary
static unordered_map<df::announcement_alert_type, AnnouncementBucket> buckets;

// updated to be the report id of the most recent report in the reports vector so we can
// track which reports we've already processed
static int newest_seen_id;

static command_result do_command(color_ostream &out, vector<string> &parameters);
static void do_cycle(color_ostream &out);

DFhackCExport command_result plugin_init(color_ostream &out, std::vector <PluginCommand> &commands) {
    DEBUG(control,out).print("initializing %s\n", plugin_name);
    commands.push_back(PluginCommand(
        plugin_name,
        "Prevent important announcements from getting lost.",
        do_command));

    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {
    if (!Core::getInstance().isMapLoaded() || !World::isFortressMode()) {
        out.printerr("Cannot enable %s without a loaded fort.\n", plugin_name);
        return CR_FAILURE;
    }

    if (enable != is_enabled) {
        is_enabled = enable;
        DEBUG(control,out).print("%s from the API; persisting\n",
                                is_enabled ? "enabled" : "disabled");
        config.set_bool(CONFIG_IS_ENABLED, is_enabled);
        if (enable)
            do_cycle(out);
    } else {
        DEBUG(control,out).print("%s from the API, but already %s; no action\n",
                                is_enabled ? "enabled" : "disabled",
                                is_enabled ? "enabled" : "disabled");
    }
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out) {
    DEBUG(control,out).print("shutting down %s\n", plugin_name);
    return CR_OK;
}

static void load_bucket_defaults(color_ostream &out) {
    static const size_t DEFAULT_RESERVED_SIZE = 20;

    size_t cumulative_reserved_size = 0;
    FOR_ENUM_ITEMS(announcement_alert_type, aat) {
        size_t reserved_size = DEFAULT_RESERVED_SIZE;
        // set specific custom defaults
        switch (aat) {
        using namespace df::enums::announcement_alert_type;
        case SPARRING: reserved_size = 5; break;
        default:
            break;
        }
        buckets[aat].reserved_size = reserved_size;
        cumulative_reserved_size += reserved_size;
    }
    DEBUG(control,out).print("default cumulative reserved size: %zd\n", cumulative_reserved_size);
}

static void clear_state() {
    for (auto &[_, bucket] : buckets)
        bucket.elems.clear();
    newest_seen_id = -1;
}

static df::announcement_alert_type get_bucket_id(df::announcement_type at) {
    if (at < 0 || at > ENUM_LAST_ITEM(announcement_type))
        return df::announcement_alert_type::GENERAL;
    return ENUM_ATTR(announcement_type, alert_type, at);
}

static void add_to_bucket(const df::report * rep) {
    auto & bucket = buckets[get_bucket_id(rep->type)];
    if (bucket.reserved_size) {
        bucket.elems.emplace_back(*rep);
        if (bucket.elems.size() > bucket.reserved_size)
            bucket.elems.pop_front();
    }
}

static bool is_reserved(const df::report * rep) {
    auto & elems = buckets[get_bucket_id(rep->type)].elems;
    if (elems.empty())
        return false;
    int min = -1, max = (int)elems.size();
    for (;;) {
        int mid = (min + max)>>1;
        if (mid == min)
            return false;
        int midv = elems[mid].id;
        if (midv == rep->id)
            return true;
        else if (midv < rep->id)
            min = mid;
        else
            max = mid;
    }
}

static void full_refresh(color_ostream &out) {
    clear_state();
    refresh_cycle_timestamp = world->frame_counter;
    auto & reports = world->status.reports;
    for (auto rep : reports)
        add_to_bucket(rep);
    if (!reports.empty())
        newest_seen_id = reports[reports.size() - 1]->id;
}

DFhackCExport command_result plugin_load_site_data (color_ostream &out) {
    cycle_timestamp = 0;
    refresh_cycle_timestamp = 0;

    config = World::GetPersistentSiteData(CONFIG_KEY);

    load_bucket_defaults(out);

    if (!config.isValid()) {
        DEBUG(control,out).print("no config found in this save; initializing\n");
        config = World::AddPersistentSiteData(CONFIG_KEY);
        config.set_bool(CONFIG_IS_ENABLED, is_enabled);
    }

    is_enabled = config.get_bool(CONFIG_IS_ENABLED);
    DEBUG(control,out).print("loading persisted enabled state: %s\n",
                            is_enabled ? "true" : "false");

    vector<string> settings;
    size_t cumulative_reserved_size = 0;
    split_string(&settings, config.get_str(), "/");
    for (auto & setting : settings) {
        vector<string> elems;
        split_string(&elems, setting, "=");
        if (elems.size() != 2)
            continue;
        df::announcement_alert_type bucket_id = (df::announcement_alert_type)string_to_int(elems[0], -1);
        if (!buckets.contains(bucket_id))
            continue;
        size_t reserved_size = string_to_int(elems[1], -1);
        buckets[bucket_id].reserved_size = reserved_size;
        cumulative_reserved_size += reserved_size;
        if (cumulative_reserved_size > MAX_TOTAL_RESERVED) {
            WARN(control,out).print(
                "announcements: cumulative reserved size too large (%zd); reverting to defaults\n", cumulative_reserved_size);
            load_bucket_defaults(out);
            break;
        }
    }

    full_refresh(out);

    return CR_OK;
}

DFhackCExport command_result plugin_save_site_data (color_ostream &out) {
    vector<string> settings;
    for (auto &[bucket_id, bucket] : buckets)
        settings.push_back(int_to_string(bucket_id) + "=" + int_to_string(bucket.reserved_size));
    config.set_str(join_strings("/", settings));
    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    if (event == DFHack::SC_WORLD_UNLOADED) {
        if (is_enabled) {
            DEBUG(control,out).print("world unloaded; disabling %s\n",
                                    plugin_name);
            is_enabled = false;
        }
        clear_state();
    }
    return CR_OK;
}

DFhackCExport command_result plugin_onupdate(color_ostream &out) {
    if (world->frame_counter - cycle_timestamp >= CYCLE_TICKS)
        do_cycle(out);
    if (world->frame_counter - refresh_cycle_timestamp >= REFRESH_CYCLE_TICKS)
        full_refresh(out);
    return CR_OK;
}

static command_result do_command(color_ostream &out, vector<string> &parameters) {
    CoreSuspender suspend;

    if (!World::isFortressMode() || !Core::getInstance().isMapLoaded()) {
        out.printerr("Cannot run %s without a loaded fort.\n", plugin_name);
        return CR_FAILURE;
    }

    bool show_help = false;
    if (!Lua::CallLuaModuleFunction(out, "plugins.announcements", "parse_commandline", std::make_tuple(parameters),
            1, [&](lua_State *L) {
                show_help = !lua_toboolean(L, -1);
            })) {
        return CR_FAILURE;
    }

    return show_help ? CR_WRONG_USAGE : CR_OK;
}

/////////////////////////////////////////////////////
// cycle logic
//

static void get_new_reports(color_ostream &out, const vector<df::report *> & reports) {
    size_t added = 0;
    for (int idx = (int)reports.size() - 1; idx >= 0; --idx) {
        auto rep = reports[idx];
        if (rep->id <= newest_seen_id)
            break;
        TRACE(cycle,out).print("adding report %d: %s\n", rep->id, ENUM_KEY_STR(announcement_type, rep->type).c_str());
        add_to_bucket(rep);
        ++added;
    }
    if (added) {
        DEBUG(cycle,out).print("added %zd new report(s)\n", added);
    }
}

static void reinstate_reports(color_ostream &out, vector<df::report *> & reports) {
    int oldest_id = reports.empty() ? INT32_MAX : reports[0]->id;
    size_t reinstated = 0;
    for (auto &[_, bucket] : buckets) {
        if (bucket.elems.empty() || bucket.elems.front().id > oldest_id)
            continue;
        for (auto & elem : bucket.elems) {
            if (elem.id >= oldest_id)
                break;
            TRACE(cycle,out).print("reinstating report %d: %s\n", elem.id, ENUM_KEY_STR(announcement_type, elem.type).c_str());
            df::report * new_rep = new df::report();
            *new_rep = elem;
            insert_into_vector(reports, &df::report::id, new_rep);
            ++reinstated;
            // we could potentially remember whether the report had an associated announcement and restore it here
        }
    }
    if (reinstated) {
        DEBUG(cycle,out).print("reinstated %zd report(s)\n", reinstated);
    }
}

static void scrub_reports(color_ostream &out, vector<df::report *> & reports) {
    if (reports.size() <= MAX_TOTAL_ANNOUNCEMENTS)
        return;

    auto &announcements = world->status.announcements;

    const size_t num_reports = reports.size();

    size_t kept = 0;
    size_t remaining = num_reports - MAX_TOTAL_ANNOUNCEMENTS;
    DEBUG(cycle,out).print("evicting %zd report(s)\n", remaining);

    // delete evicted reports and compact vector elements
    for (size_t idx = 0; idx < num_reports; ++idx) {
        auto rep = reports[idx];
        if (remaining > 0 && !is_reserved(rep)) {
            TRACE(cycle,out).print("evicting report %d: %s\n", rep->id, ENUM_KEY_STR(announcement_type, rep->type).c_str());
            if (rep->flags.bits.announcement)
                erase_from_vector(announcements, &df::report::id, rep->id);
            delete rep;
            --remaining;
        } else {
            if (idx > kept)
                reports[kept] = reports[idx];
            ++kept;
        }
    }

    reports.resize(kept);
}

static void do_cycle(color_ostream &out) {
    cycle_timestamp = world->frame_counter;

    TRACE(cycle,out).print("running %s cycle\n", plugin_name);

    auto &reports = world->status.reports;

    // add new reports to our buckets
    get_new_reports(out, reports);

    // reinstate reserved reports that are older than the current oldest ID
    reinstate_reports(out, reports);

    // if we are over our threshold, evict oldest items (respecting reserved elements)
    scrub_reports(out, reports);

    newest_seen_id = reports.empty() ? -1 : reports[reports.size()-1]->id;
}
