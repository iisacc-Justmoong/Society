import QtQuick

// This model belongs to the application window, not to any lazy-loaded page.
QtObject {
    id: root
    objectName: "notificationCenter"

    property ListModel entries: ListModel {}
    readonly property int count: entries.count
    property var activeBySource: ({})

    function record(source, message) {
        const key = String(source || "")
        const current = String(message || "").split(/\r?\n/)
            .map(line => line.trim()).filter(line => line.length > 0)
        const previous = activeBySource[key] || []
        for (const line of current) {
            if (previous.indexOf(line) !== -1) continue
            let present = false
            for (let i = 0; i < entries.count; ++i) {
                const entry = entries.get(i)
                if (entry.source === key && entry.message === line) { present = true; break }
            }
            if (!present) entries.append({source: key, message: line})
        }
        activeBySource[key] = current
    }

    // Retain the source's active state so a dismissed warning does not return
    // until that warning clears and subsequently recurs.
    function dismiss(index) {
        if (index >= 0 && index < entries.count) entries.remove(index)
    }

    function clear() { entries.clear() }
}
