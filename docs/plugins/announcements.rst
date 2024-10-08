announcements
=============

.. dfhack-tool::
    :summary: Prevent important announcements from getting lost.
    :tags: fort bugfix

This tool ensures that announcements of interesting events are not lost, even
when many announcements of other types are being generated.

Announcements are what cause those little notification bubbles on the left side
of the screen. This tool is needed because Dwarf Fortress has limited space to
remember recent announcements, and as a new announcements come in, the oldest
announcements are forgotten. If you have ever clicked on an announcement bubble
and saw only an empty list, then you have seen what happens when announcements
get evicted from the history list.

This tool changes the announcement management logic so that the most recent
announcements *per announcement type* are always kept in the history. This
means that your announcement bubble for a new marriage will still contain
information when you click on it, even if your sparring military has been
spamming the announcement history with "lightly tapped" messages.

Optionally, this tool can also remove all sparring announcements entirely.
There is no other way to disable sparring messages without losing announcements
for all combat in general.

This tool is enabled by default, but you can toggle it in `gui/control-panel`
on the ``Bug fixes`` tab.

Usage
-----

::

    enable announcements
    announcements [status]
    announcements enable|disable remove-sparring
    announcements set <type> <value>

You can set a custom number to protect for each type of announcement. You can
see the category names by running::

    :lua @df.announcement_alert_type

There is a limit of 2000 for the number of reports to protect, cumulative
across all types. If you exceed that limit, you will not be able to increase the
setting for one type until you reduce the setting for other types to make room.

The defaults protect the most recent 20 announcements of each type, except for
``sparring``, which only protects the most recent 5.

Examples
--------

``announcements``
    List the current configuration and give a summary of how many reports of
    each type are currently in DF's memory.
``announcements enable remove-sparring``
    Remove military sparring messages from the announcements as soon as they
    are added, preventing them from bothering you.
``announcements set marriage 100``
    Ensure the 100 most recent marriage events are always available for viewing
    in the vanilla announcements UI.
