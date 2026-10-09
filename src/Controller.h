#pragma once
#include "link/Service.h"
#include "link/PeerHub.h"
#include "platform/Desktop.h"
#include <QObject>
#include <QSet>
#include <QThread>
#include <QVariantList>
#include <QWindow>

namespace Shelf {
class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList shelves READ shelves NOTIFY shelvesChanged)
    Q_PROPERTY(QVariantList offers READ offers NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY changed)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY changed)
    Q_PROPERTY(QVariantMap analysisResult READ analysisResult NOTIFY changed)
    Q_PROPERTY(QString activeShelf READ activeShelf NOTIFY changed)
    Q_PROPERTY(QString activeName READ activeName NOTIFY changed)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(QString shortcut READ shortcut WRITE setShortcut NOTIFY changed)
    Q_PROPERTY(QString shortcutWarning READ shortcutWarning NOTIFY changed)
    Q_PROPERTY(QString platformStatus READ platformStatus NOTIFY changed)
    Q_PROPERTY(QString dockEdge READ dockEdge NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY changed)
    Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY changed)
    Q_PROPERTY(bool picking READ picking NOTIFY changed)
    Q_PROPERTY(bool pickMultiple READ pickMultiple NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool linkEnabled READ linkEnabled WRITE setLinkEnabled NOTIFY changed)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    explicit Controller(QString root, bool background, QObject *parent = nullptr);
    ~Controller() override;
    void start();
    void attachWindow(QWindow *window);
    QVariantList items() const;
    QVariantList shelves() const { return m_shelves.toVariantList(); }
    QVariantList offers() const;
    QVariantList sources() const { return m_peers.sources().toVariantList(); }
    QVariantList peers() const;
    QVariantMap preferences() const;
    QVariantMap analysisResult() const { return m_analysis.toVariantMap(); }
    QString activeShelf() const { return m_active; }
    QString activeName() const;
    QString filter() const { return m_filter; }
    QString message() const { return m_message; }
    QString theme() const { return m_settings["theme"].toString("system"); }
    QString shortcut() const { return m_settings["shortcut"].toString("Ctrl+Alt+S"); }
    QString shortcutWarning() const;
    QString platformStatus() const { return m_desktop.shortcutStatus(); }
    QString dockEdge() const { return m_dock; }
    int count() const { return m_items.size(); }
    int selectedCount() const { return int(m_selected.size()); }
    int selectionRevision() const { return m_selectionRevision; }
    bool expanded() const { return m_expanded; }
    bool visible() const { return m_visible; }
    bool settingsOpen() const { return m_settingsOpen; }
    bool picking() const { return m_picking; }
    bool pickMultiple() const { return m_pickMultiple; }
    bool busy() const { return m_peers.busy(); }
    bool linkEnabled() const { return m_settings["linkEnabled"].toBool(true); }
    bool alwaysOnTop() const { return m_settings["alwaysOnTop"].toBool(false); }
    bool reducedMotion() const { return m_settings["reducedMotion"].toBool(false); }
    bool undoAvailable() const { return m_undoAvailable; }
    double progress() const { return m_progress; }
    void setFilter(QString value);
    void setTheme(QString value);
    void setShortcut(QString value);
    void setExpanded(bool value);
    void setSettingsOpen(bool value);
    void setLinkEnabled(bool value);
    void setAlwaysOnTop(bool value);
    void setReducedMotion(bool value);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void show();
    Q_INVOKABLE void collapse();
    Q_INVOKABLE void acceptDrop(QVariant urls, QString text, QString html);
    Q_INVOKABLE void paste();
    Q_INVOKABLE void select(QString id, bool additive, bool range = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool selected(QString id) const { return m_selected.contains(id); }
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void openItem(QString id);
    Q_INVOKABLE void revealItem(QString id);
    Q_INVOKABLE void copySelected(QString mode = "content");
    Q_INVOKABLE void dragSelected();
    Q_INVOKABLE void reorderItem(QString id, QString beforeId);
    Q_INVOKABLE void createShelf(QString name, bool temporary = false);
    Q_INVOKABLE void renameShelf(QString id, QString name);
    Q_INVOKABLE void deleteShelf(QString id);
    Q_INVOKABLE void switchShelf(QString id);
    Q_INVOKABLE void pinShelf(QString id, bool pin);
    Q_INVOKABLE void moveShelf(QString id, int delta);
    Q_INVOKABLE void runPeer(QString app, QString action);
    Q_INVOKABLE void runSource(QString app, QString action);
    Q_INVOKABLE void cancelJob();
    Q_INVOKABLE void dismissAnalysis() { m_analysis = {}; emit changed(); }
    Q_INVOKABLE void confirmPick();
    Q_INVOKABLE void cancelPick();
    Q_INVOKABLE void showSettings();
    Q_INVOKABLE void nativeMove();
    Q_INVOKABLE void nativeResize(int edges);
    Q_INVOKABLE void dock(QString edge);
    Q_INVOKABLE void saveGeometry(int x, int y, int w, int h);
    Q_INVOKABLE QVariantMap restoreGeometry() const;
    Q_INVOKABLE void setPeerEnabled(QString id, bool enabled);
    Q_INVOKABLE void getPeer(QString id);
    Q_INVOKABLE void setRetention(int days);
    Q_INVOKABLE void setQuota(int mib);
    Q_INVOKABLE void setStartAtLogin(bool value);
signals:
    void changed();
    void itemsChanged();
    void shelvesChanged();
    void pulse();
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void stageImage(QImage image);
    void prepareSelection();
    void saveSetting(QString key, QJsonValue value);
    void applyWindowPreferences();
    void feedback(QString message);
    QStringList selectedIds() const;
    QJsonArray cachedContents() const;
    QJsonObject item(QString id) const;
    void peerCall(QString app, QString action, QJsonArray inputs, QJsonObject options);
    bool eligible(const QJsonObject &item) const;
    QThread m_thread;
    Service *m_service;
    PeerHub m_peers;
    Desktop m_desktop;
    QWindow *m_window = nullptr;
    QString m_root, m_active, m_filter, m_message, m_anchor, m_dock;
    QJsonArray m_items, m_shelves;
    // Qt Quick reference sequences can call the getter for every indexed read.
    // Keep the converted, filtered list shared until its actual inputs change.
    mutable QVariantList m_itemsCache;
    mutable bool m_itemsCacheValid = false;
    QJsonArray m_dragContent;
    QJsonObject m_settings, m_analysis;
    QSet<QString> m_selected;
    QStringList m_pickTypes;
    bool m_expanded = false, m_visible = true, m_settingsOpen = false, m_picking = false, m_pickMultiple = false, m_undoAvailable = false;
    int m_selectionRevision = 0;
    int m_dragRevision = -1;
    double m_progress = -1;
};
}
