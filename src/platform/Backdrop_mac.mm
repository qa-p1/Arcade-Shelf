#include "Backdrop_p.h"

#include <QGuiApplication>
#include <QPointer>
#include <QSurfaceFormat>
#include <QWindow>
#include <utility>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

// A sibling underneath Qt's content view cannot intercept Qt's pointer, focus
// or native drag handling, even in the transparent margin outside the capsule.
@interface ShelfMaterialView : NSVisualEffectView
@end
@implementation ShelfMaterialView
- (NSView *)hitTest:(NSPoint)point { Q_UNUSED(point); return nil; }
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)isAccessibilityElement { return NO; }
@end

namespace Shelf {
namespace {

class MacBackdrop final : public NativeBackdrop {
public:
    MacBackdrop(QObject *context, std::function<void()> refresh)
    {
        const QPointer<QObject> guard(context);
        m_observer = [[[NSWorkspace sharedWorkspace] notificationCenter]
            addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
            object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *) {
                if (guard) refresh();
            }];
        [m_observer retain];
    }
    ~MacBackdrop() override
    {
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:m_observer];
        [m_observer release];
        clear();
    }
    bool apply(QWindow *window, const QRectF &region, qreal radius, bool dark,
               QString *status) override
    {
        @autoreleasepool {
            if (QGuiApplication::platformName() != QStringLiteral("cocoa")) {
                clear(); *status = QStringLiteral("Native material unavailable on this display backend"); return false;
            }
            if ([[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceTransparency]) {
                clear(); *status = QStringLiteral("Native material disabled by macOS Reduce Transparency"); return false;
            }
            NSView *qtView = reinterpret_cast<NSView *>(window->winId());
            NSWindow *nativeWindow = [qtView window];
            NSView *parent = [qtView superview];
            if (!qtView || !nativeWindow || !parent || [nativeWindow contentView] != qtView
                || [qtView isOpaque] || !window->format().hasAlpha()) {
                clear(); *status = QStringLiteral("macOS material requires a transparent native Qt content view"); return false;
            }
            if (m_effect && [m_effect superview] != parent) clear();
            if (!m_effect) {
                m_effect = [[ShelfMaterialView alloc] initWithFrame:NSZeroRect];
                [m_effect setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
                [m_effect setMaterial:NSVisualEffectMaterialHUDWindow];
                [m_effect setState:NSVisualEffectStateActive];
                [m_effect setWantsLayer:YES];
                // Preserve Qt's original contentView and Metal layer. Making
                // the material a child of Qt's view would cover Quick content;
                // replacing its contentView would break QCocoaWindow ownership.
                [parent addSubview:m_effect positioned:NSWindowBelow relativeTo:qtView];
            }
            const CGFloat y = [qtView isFlipped] ? region.y()
                : [qtView bounds].size.height - region.y() - region.height();
            const NSRect logical = NSMakeRect(region.x(), y, region.width(), region.height());
            [m_effect setFrame:[qtView convertRect:logical toView:parent]];
            [m_effect setAppearance:[NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua]];
            [m_effect layer].cornerRadius = radius;
            [m_effect layer].masksToBounds = YES;
            [nativeWindow setOpaque:NO];
            [nativeWindow setBackgroundColor:[NSColor clearColor]];
            *status = QStringLiteral("macOS system material");
            return true;
        }
    }
    void clear() override
    {
        @autoreleasepool {
            [m_effect removeFromSuperview];
            [m_effect release];
            m_effect = nil;
        }
    }
private:
    ShelfMaterialView *m_effect = nil;
    id m_observer = nil;
};
} // namespace

std::unique_ptr<NativeBackdrop> makeNativeBackdrop(QObject *context, std::function<void()> refresh)
{
    return std::make_unique<MacBackdrop>(context, std::move(refresh));
}

} // namespace Shelf
