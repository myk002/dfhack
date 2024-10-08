local _ENV = mkmodule('plugins.announcements')

local argparse = require('argparse')

------------------
-- command line
--

local function print_status()
    print(('announcements is %senabled'):format(isEnabled() and '' or 'not '))
    print(('sparring announcements are %sbeing removed'):format(announcements_getRemoveSparring() and '' or 'not '))
    print()
    local report_counts = announcements_getReportCountByType()
    local fmt = '%21s  %8s  %10s'
    print(fmt:format('Type', 'Reserved', 'In history'))
    print(fmt:format(('-'):rep(20), ('-'):rep(8), ('-'):rep(10)))
    for val,name in ipairs(df.announcement_alert_type) do
        print(fmt:format(name:lower(), announcements_getReserved(val), report_counts[val] or 0))
    end
end

local function do_set(name, val)
    local id = df.announcement_alert_type[name:upper()]
    if not id then
        qerror(('unknown announcement type: "%s"'):format(name))
    end
    announcements_setReserved(id, val)
end

function parse_commandline(args)
    local opts = {}
    local positionals = argparse.processArgsGetopt(args, {
        {'h', 'help', handler=function() opts.help = true end},
    })

    if opts.help or not positionals or positionals[1] == 'help' then
        return false
    end

    local command = table.remove(positionals, 1)
    if not command or command == 'status' then
        print_status()
    elseif command == 'enable' or command == 'disable' then
        if positionals[1] == 'remove-sparring' then
            announcements_setRemoveSparring(command == 'enable')
        else
            qerror(('unknown feature: "%s"'):format(positionals[1]))
        end
    elseif command == 'set' then
        do_set(positionals[1], argparse.nonnegativeInt(positionals[2]))
    else
        return false
    end

    return true
end

return _ENV
