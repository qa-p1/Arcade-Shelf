pragma ComponentBehavior: Bound
import QtQuick

// The muted sentence under a dialog title.
Label {
    color: Theme.muted
    wrapMode: Text.Wrap
    lineHeight: Math.round(Theme.bodySize * 1.55)
}
