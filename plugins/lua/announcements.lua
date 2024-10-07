local _ENV = mkmodule('plugins.announcements')

local argparse = require('argparse')

------------------
-- command line
--

local function print_status()
    print(('announcements is %senabled'):format(isEnabled() and '' or 'not '))
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
    else
        return false
    end

    return true
end

return _ENV
