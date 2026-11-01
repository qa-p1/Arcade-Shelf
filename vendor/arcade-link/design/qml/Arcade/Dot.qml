pragma ComponentBehavior: Bound
import QtQuick

// A 7 px status dot: tone "neutral", "ok", "warn", "danger" or "accent".
Rectangle {
    property string tone: "neutral"
    implicitWidth: 7
    implicitHeight: 7
    radius: 3.5
    color: tone === "ok" ? Theme.ok : tone === "warn" ? Theme.warn : tone === "danger" ? Theme.danger : tone === "accent" ? Theme.accent : Theme.faint
}
