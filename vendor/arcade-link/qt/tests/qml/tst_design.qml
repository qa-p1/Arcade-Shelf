// Loads the QML design kit (design/qml/Arcade): theme switching, accents,
// format helpers, icons and Inter.
import QtQuick
import QtTest
import Arcade

TestCase {
    name: "Design"

    Icon { id: icon; name: "settings" }
    Icon { id: filled; name: "playFilled"; size: 24 }

    function test_theme_and_accent() {
        Theme.appId = "arcade.box"
        Theme.mode = "light"
        compare(String(Theme.accent), "#724fff")
        compare(String(Theme.bg), "#f6f7f9")
        Theme.mode = "dark"
        compare(String(Theme.accent), "#a58fff")
        compare(String(Theme.accentText), "#0b0d10")
        compare(String(Theme.bg), "#111317")
        compare(String(Theme.accentFor("arcade.shelf", "soft")), "#32223a")
        compare(Theme.bodySize, 13)
        compare(Theme.radiusLg, 12)
        Theme.mode = "system"
    }

    function test_format() {
        compare(Format.formatSize(940000), "940 kB")
        compare(Format.formatSize(12400000), "12.4 MB")
        compare(Format.formatCount(1234567), "1,234,567")
        compare(Format.formatDuration(3909, "media"), "1:05:09")
        compare(Format.formatDuration(150, "job"), "2 min")
        compare(Format.formatRelative(1761662100000, 1761661800000 + 300000 + 300000, 0), "5 min ago")
    }

    function test_icons_and_font() {
        compare(icon.implicitWidth, Theme.iconSize)
        verify(!icon.filled)
        verify(filled.filled)
        tryCompare(Fonts.inter, "status", FontLoader.Ready)
        compare(Fonts.sans, "Inter Variable")
    }
}
