// Cocoa platform layer (ARC). One NSWindow/NSPanel per NativeWindow with a layer-backed
// NSView whose backing layer is a CAMetalLayer that Dawn presents into.
#include "Platform.hpp"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <Carbon/Carbon.h> // kVK_* key codes only; no Carbon linkage needed

#include <algorithm>
#include <cmath>

using Terracotta2::Key;
using Terracotta2::KeyAction;
using Terracotta2::MouseButton;
using Terracotta2::platform::NativeWindow;
using Terracotta2::platform::WindowEvents;

// ---------------------------------------------------------------------------------------------
// View: input, layer, window delegate.
// ponytail: no NSTextInputClient, so no IME composition / dead keys / emoji picker; text comes from
// -[NSEvent characters]. Add the protocol (and route insertText:/setMarkedText:) when CJK input matters.
@interface TCView : NSView <NSWindowDelegate>
@property(nonatomic, assign) WindowEvents* events; // null once the owning NativeWindow is destroyed
@property(nonatomic, strong) NSCursor* cursor;
@end

static int modsOf(NSEventModifierFlags f) {
    int m = 0;
    if (f & NSEventModifierFlagShift) m |= Terracotta2::ModShift;
    if (f & NSEventModifierFlagControl) m |= Terracotta2::ModCtrl;
    if (f & NSEventModifierFlagOption) m |= Terracotta2::ModAlt;
    if (f & NSEventModifierFlagCommand) m |= Terracotta2::ModSuper;
    return m;
}

static Key keyOf(unsigned short code) {
    switch (code) {
        case kVK_LeftArrow: return Key::Left;
        case kVK_RightArrow: return Key::Right;
        case kVK_UpArrow: return Key::Up;
        case kVK_DownArrow: return Key::Down;
        case kVK_Home: return Key::Home;
        case kVK_End: return Key::End;
        case kVK_PageUp: return Key::PageUp;
        case kVK_PageDown: return Key::PageDown;
        case kVK_Delete: return Key::Backspace;
        case kVK_ForwardDelete: return Key::Delete;
        case kVK_Return: case kVK_ANSI_KeypadEnter: return Key::Enter;
        case kVK_Escape: return Key::Escape;
        case kVK_Tab: return Key::Tab;
        case kVK_Space: return Key::Space;
        case kVK_ANSI_A: return Key::A; case kVK_ANSI_B: return Key::B; case kVK_ANSI_C: return Key::C;
        case kVK_ANSI_D: return Key::D; case kVK_ANSI_E: return Key::E; case kVK_ANSI_F: return Key::F;
        case kVK_ANSI_G: return Key::G; case kVK_ANSI_H: return Key::H; case kVK_ANSI_I: return Key::I;
        case kVK_ANSI_J: return Key::J; case kVK_ANSI_K: return Key::K; case kVK_ANSI_L: return Key::L;
        case kVK_ANSI_M: return Key::M; case kVK_ANSI_N: return Key::N; case kVK_ANSI_O: return Key::O;
        case kVK_ANSI_P: return Key::P; case kVK_ANSI_Q: return Key::Q; case kVK_ANSI_R: return Key::R;
        case kVK_ANSI_S: return Key::S; case kVK_ANSI_T: return Key::T; case kVK_ANSI_U: return Key::U;
        case kVK_ANSI_V: return Key::V; case kVK_ANSI_W: return Key::W; case kVK_ANSI_X: return Key::X;
        case kVK_ANSI_Y: return Key::Y; case kVK_ANSI_Z: return Key::Z;
        case kVK_ANSI_0: return Key::Num0; case kVK_ANSI_1: return Key::Num1; case kVK_ANSI_2: return Key::Num2;
        case kVK_ANSI_3: return Key::Num3; case kVK_ANSI_4: return Key::Num4; case kVK_ANSI_5: return Key::Num5;
        case kVK_ANSI_6: return Key::Num6; case kVK_ANSI_7: return Key::Num7; case kVK_ANSI_8: return Key::Num8;
        case kVK_ANSI_9: return Key::Num9;
        case kVK_F1: return Key::F1; case kVK_F2: return Key::F2; case kVK_F3: return Key::F3;
        case kVK_F4: return Key::F4; case kVK_F5: return Key::F5; case kVK_F6: return Key::F6;
        case kVK_F7: return Key::F7; case kVK_F8: return Key::F8; case kVK_F9: return Key::F9;
        case kVK_F10: return Key::F10; case kVK_F11: return Key::F11; case kVK_F12: return Key::F12;
        default: return Key::Unknown;
    }
}

@implementation TCView {
    NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
        _cursor = [NSCursor arrowCursor];
    }
    return self;
}

- (CALayer*)makeBackingLayer {
    CAMetalLayer* l = [CAMetalLayer layer];
    l.opaque = YES;
    return l;
}
- (BOOL)wantsUpdateLayer { return YES; }
- (void)updateLayer {}
- (BOOL)isFlipped { return YES; } // y-down local coordinates
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }

- (void)syncScale {
    self.layer.contentsScale = self.window ? self.window.backingScaleFactor : 1.0;
}
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self syncScale];
}
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self syncScale];
    if (_events) _events->onResize();
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingInVisibleRect | NSTrackingActiveAlways | NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (void)resetCursorRects {
    [self addCursorRect:self.bounds cursor:_cursor];
}

// ---- mouse ----
- (void)sendMove:(NSEvent*)e {
    if (!_events) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    _events->onMouseMove((float)p.x, (float)p.y);
}
- (void)sendButton:(NSEvent*)e button:(MouseButton)b down:(BOOL)down {
    if (!_events) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    _events->onMouseButton((float)p.x, (float)p.y, b, down, modsOf(e.modifierFlags));
}
- (void)mouseMoved:(NSEvent*)e { [self sendMove:e]; }
- (void)mouseDragged:(NSEvent*)e { [self sendMove:e]; }
- (void)rightMouseDragged:(NSEvent*)e { [self sendMove:e]; }
- (void)otherMouseDragged:(NSEvent*)e { [self sendMove:e]; }
- (void)mouseDown:(NSEvent*)e { [self sendButton:e button:MouseButton::Left down:YES]; }
- (void)mouseUp:(NSEvent*)e { [self sendButton:e button:MouseButton::Left down:NO]; }
- (void)rightMouseDown:(NSEvent*)e { [self sendButton:e button:MouseButton::Right down:YES]; }
- (void)rightMouseUp:(NSEvent*)e { [self sendButton:e button:MouseButton::Right down:NO]; }
- (void)otherMouseDown:(NSEvent*)e {
    if (e.buttonNumber == 2) [self sendButton:e button:MouseButton::Middle down:YES];
}
- (void)otherMouseUp:(NSEvent*)e {
    if (e.buttonNumber == 2) [self sendButton:e button:MouseButton::Middle down:NO];
}
- (void)mouseExited:(NSEvent*)e {
    if (_events) _events->onMouseLeave();
}
- (void)scrollWheel:(NSEvent*)e {
    if (!_events) return;
    float dx = (float)e.scrollingDeltaX, dy = (float)e.scrollingDeltaY;
    if (!e.hasPreciseScrollingDeltas) { // notched wheel: lines -> px
        dx *= 10;
        dy *= 10;
    }
    if (dx != 0 || dy != 0) _events->onScroll(dx, dy);
}

// ---- keyboard ----
// Modifier keys alone (flagsChanged) are not reported: there is no Key for them and every
// key/mouse event already carries the current Mod flags.
- (void)keyDown:(NSEvent*)e {
    if (!_events) return;
    int mods = modsOf(e.modifierFlags);
    _events->onKey(keyOf(e.keyCode), e.isARepeat ? KeyAction::Repeat : KeyAction::Press, mods);
    if (mods & (Terracotta2::ModSuper | Terracotta2::ModCtrl)) return; // shortcuts, not text
    NSString* s = e.characters;
    for (NSUInteger i = 0; _events && i < s.length; ++i) {
        uint32_t c = [s characterAtIndex:i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.length) { // surrogate pair
            uint32_t lo = [s characterAtIndex:i + 1];
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            ++i;
        }
        if (c < 0x20 || c == 0x7F || (c >= 0xF700 && c <= 0xF8FF)) continue; // control / function keys
        _events->onChar(c);
    }
}
- (void)keyUp:(NSEvent*)e {
    if (_events) _events->onKey(keyOf(e.keyCode), KeyAction::Release, modsOf(e.modifierFlags));
}

// ---- window delegate ----
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (_events) _events->onCloseRequest();
    return NO; // Application destroys the window (deferred)
}
- (void)windowDidBecomeKey:(NSNotification*)n {
    if (_events) _events->onActivate(true);
}
- (void)windowDidResignKey:(NSNotification*)n {
    if (_events) _events->onActivate(false);
}
- (void)windowDidResize:(NSNotification*)n {
    if (!_events) return;
    _events->onResize();
    _events->onRedraw(); // live resize runs in a nested loop: repaint now so content follows the edge
}
@end

// ---------------------------------------------------------------------------------------------
@interface TCAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation TCAppDelegate
- (void)quit:(id)sender {
    try { Terracotta2::Application::get().quit(); } catch (...) {} // no live Application: nothing to stop
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
    [self quit:nil]; // Dock > Quit, logout, ...: end run() so destructors run
    return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { return NO; }
@end

// ---------------------------------------------------------------------------------------------
namespace Terracotta2::platform {

struct NativeWindow {
    NSWindow* window = nil;
    TCView* view = nil;
    Cursor cursor = Cursor::Arrow;
};

namespace {
std::vector<NativeWindow*> g_windows;
TCAppDelegate* g_delegate = nil; // NSApp.delegate is weak
std::function<void()> g_onDeactivate;
id g_deactivateObserver = nil;

CGFloat primaryHeight() {
    NSArray<NSScreen*>* s = [NSScreen screens];
    return s.count ? s[0].frame.size.height : 0;
}
} // namespace

void init() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        g_delegate = [TCAppDelegate new];
        [NSApp setDelegate:g_delegate];

        NSMenu* bar = [NSMenu new];
        NSMenuItem* appItem = [NSMenuItem new];
        [bar addItem:appItem];
        NSMenu* appMenu = [NSMenu new];
        NSString* name = [[NSProcessInfo processInfo] processName];
        NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:name]
                                                      action:@selector(quit:)
                                               keyEquivalent:@"q"];
        quit.target = g_delegate;
        [appMenu addItem:quit];
        appItem.submenu = appMenu;
        [NSApp setMainMenu:bar];

        g_deactivateObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSApplicationDidResignActiveNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification*) { if (g_onDeactivate) g_onDeactivate(); }];

        [NSApp finishLaunching];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}

void shutdown() {
    if (g_deactivateObserver) [[NSNotificationCenter defaultCenter] removeObserver:g_deactivateObserver];
    g_deactivateObserver = nil;
    g_onDeactivate = nullptr;
    g_windows.clear();
    [NSApp setDelegate:nil];
    g_delegate = nil;
}

NativeWindow* createWindow(const WindowDesc& desc, WindowEvents* events, NativeWindow* owner) {
    @autoreleasepool {
        NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        if (desc.customTitlebar) mask |= NSWindowStyleMaskFullSizeContentView; // content under the titlebar
        const bool centered = std::isnan(desc.x) || std::isnan(desc.y);
        // desc.x/y: content top-left in y-down screen px -> Cocoa bottom-left origin.
        NSRect content = NSMakeRect(centered ? 0 : desc.x, centered ? 0 : primaryHeight() - desc.y - desc.height,
                                    desc.width, desc.height);
        NSWindow* win;
        if (desc.popup) {
            // Non-activating borderless panel above everything: clicks reach it while the owner
            // stays the key window, so keyboard input keeps flowing through the owner.
            NSPanel* p = [[NSPanel alloc] initWithContentRect:content
                                                    styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
            p.floatingPanel = YES;
            p.level = NSPopUpMenuWindowLevel; // after floatingPanel, which resets the level
            p.becomesKeyOnlyIfNeeded = YES;
            p.hidesOnDeactivate = NO; // the app closes its popups itself on deactivate
            p.hasShadow = YES;
            p.opaque = NO;            // transparent outside the rounded corners
            p.backgroundColor = NSColor.clearColor;
            p.alphaValue = 0;         // on screen (so Metal hands out drawables) but invisible until showPopup()
            win = p;
        } else if (owner) { // floating tool window
            NSPanel* p = [[NSPanel alloc] initWithContentRect:content styleMask:mask backing:NSBackingStoreBuffered defer:NO];
            // Ordinary z-order: can go behind the main window, never hides on deactivate.
            p.floatingPanel = NO;
            p.hidesOnDeactivate = NO;
            p.becomesKeyOnlyIfNeeded = NO; // must take key focus so text input works
            win = p;
        } else {
            win = [[NSWindow alloc] initWithContentRect:content styleMask:mask backing:NSBackingStoreBuffered defer:NO];
        }
        win.releasedWhenClosed = NO;
        win.acceptsMouseMovedEvents = YES;
        win.title = [NSString stringWithUTF8String:desc.title.c_str()];
        if (desc.customTitlebar) { // keep the traffic lights, drop the bar and its title
            win.titlebarAppearsTransparent = YES;
            win.titleVisibility = NSWindowTitleHidden;
        }
        if (centered) [win center];

        TCView* view = [[TCView alloc] initWithFrame:[win contentRectForFrameRect:win.frame]];
        view.events = events;
        win.contentView = view;
        win.delegate = view;
        if (desc.popup) {
            if (desc.cornerRadius > 0) {
                view.layer.cornerRadius = desc.cornerRadius;
                view.layer.masksToBounds = YES;
            }
            [win orderFront:nil];
        } else {
            [win makeFirstResponder:view];
            [win makeKeyAndOrderFront:nil];
        }

        auto* nw = new NativeWindow;
        nw->window = win;
        nw->view = view;
        g_windows.push_back(nw);
        return nw;
    }
}

void destroyWindow(NativeWindow* w) {
    if (!w) return;
    @autoreleasepool {
        w->view.events = nullptr;
        w->window.delegate = nil;
        [w->window orderOut:nil];
        [w->window close];
        g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), w), g_windows.end());
        delete w; // ARC releases window + view
    }
}

wgpu::Surface createSurface(const wgpu::Instance& instance, NativeWindow* w) {
    wgpu::SurfaceSourceMetalLayer src;
    src.layer = (__bridge void*)w->view.layer;
    wgpu::SurfaceDescriptor sd;
    sd.nextInChain = &src;
    return instance.CreateSurface(&sd);
}

SkISize framebufferSize(NativeWindow* w) {
    NSRect b = [w->view convertRectToBacking:w->view.bounds];
    return SkISize::Make((int)std::lround(b.size.width), (int)std::lround(b.size.height));
}
SkSize logicalSize(NativeWindow* w) {
    NSSize s = w->view.bounds.size;
    return SkSize::Make((float)s.width, (float)s.height);
}
float contentScale(NativeWindow* w) { return (float)w->window.backingScaleFactor; }

SkPoint contentScreenPos(NativeWindow* w) {
    NSRect cr = [w->window contentRectForFrameRect:w->window.frame];
    return SkPoint::Make((float)cr.origin.x, (float)(primaryHeight() - NSMaxY(cr)));
}
void setContentScreenPos(NativeWindow* w, SkPoint p) {
    NSRect fr = w->window.frame;
    NSRect cr = [w->window contentRectForFrameRect:fr];
    NSPoint want = NSMakePoint(p.x(), primaryHeight() - p.y() - cr.size.height); // content bottom-left
    [w->window setFrameOrigin:NSMakePoint(want.x - (cr.origin.x - fr.origin.x), want.y - (cr.origin.y - fr.origin.y))];
}

void showPopup(NativeWindow* w) {
    w->window.alphaValue = 1;
    [w->window invalidateShadow]; // the shadow follows the (rounded) content, now drawn
}

void setContentSize(NativeWindow* w, SkSize s) {
    const SkPoint topLeft = contentScreenPos(w);
    [w->window setContentSize:NSMakeSize(s.width(), s.height())];
    setContentScreenPos(w, topLeft); // Cocoa keeps the bottom-left fixed; keep the top-left instead
}

SkRect workArea(SkPoint screen) {
    const CGFloat ph = primaryHeight();
    const NSPoint p = NSMakePoint(screen.x(), ph - screen.y());
    NSScreen* target = NSScreen.mainScreen;
    for (NSScreen* s in NSScreen.screens)
        if (NSPointInRect(p, s.frame)) { target = s; break; }
    const NSRect v = target.visibleFrame; // minus menu bar and Dock
    return SkRect::MakeXYWH((float)v.origin.x, (float)(ph - NSMaxY(v)), (float)v.size.width, (float)v.size.height);
}

void setOnDeactivate(std::function<void()> fn) { g_onDeactivate = std::move(fn); }

void startWindowDrag(NativeWindow* w) {
    NSEvent* e = NSApp.currentEvent;
    if (e.type != NSEventTypeLeftMouseDown) return; // called outside a real press (tests)
    if (e.clickCount == 2) { // the user's "double-click a window's title bar to" setting
        NSString* action = [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
        if ([action isEqualToString:@"Minimize"]) [w->window performMiniaturize:nil];
        else if (![action isEqualToString:@"None"]) [w->window performZoom:nil];
        return;
    }
    [w->window performWindowDragWithEvent:e];
}
void minimize(NativeWindow* w) { [w->window performMiniaturize:nil]; }
void toggleMaximize(NativeWindow* w) { [w->window performZoom:nil]; }
bool isMaximized(NativeWindow* w) { return w->window.isZoomed; }

void setTitle(NativeWindow* w, const std::string& title) {
    w->window.title = [NSString stringWithUTF8String:title.c_str()];
}
void raise(NativeWindow* w) { [w->window makeKeyAndOrderFront:nil]; }

void setCursor(NativeWindow* w, Cursor c) {
    if (c == w->cursor) return;
    w->cursor = c;
    NSCursor* nc;
    switch (c) {
        case Cursor::IBeam: nc = [NSCursor IBeamCursor]; break;
        case Cursor::Hand: nc = [NSCursor pointingHandCursor]; break;
        case Cursor::HResize: nc = [NSCursor resizeLeftRightCursor]; break;
        case Cursor::VResize: nc = [NSCursor resizeUpDownCursor]; break;
        case Cursor::Move: nc = [NSCursor openHandCursor]; break;
        default: nc = [NSCursor arrowCursor]; break;
    }
    w->view.cursor = nc;
    [w->window invalidateCursorRectsForView:w->view]; // re-runs -resetCursorRects
    [nc set];
}

std::vector<NativeWindow*> windowsFrontToBack() {
    std::vector<NativeWindow*> out;
    for (NSWindow* nsw in [NSApp orderedWindows]) {
        if (!nsw.visible) continue;
        for (NativeWindow* n : g_windows)
            if (n->window == nsw) { out.push_back(n); break; }
    }
    return out;
}

void pollEvents() {
    for (;;) {
        @autoreleasepool {
            NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!e) return;
            [NSApp sendEvent:e];
            [NSApp updateWindows];
        }
    }
}

void waitEvents() {
    @autoreleasepool {
        NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantFuture]
                                           inMode:NSDefaultRunLoopMode dequeue:YES];
        if (e) {
            [NSApp sendEvent:e];
            [NSApp updateWindows];
        }
    }
    pollEvents();
}

void wake() {
    // Posted unconditionally (thread-safe). If nobody is waiting the event is consumed as a no-op
    // by the next poll, costing one extra loop iteration at most.
    NSEvent* e = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0
                                   timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    [NSApp postEvent:e atStart:NO];
}

int modifiers() { return modsOf(NSEvent.modifierFlags); }

std::string clipboard() {
    @autoreleasepool {
        NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        return s ? std::string([s UTF8String]) : std::string();
    }
}
void setClipboard(const std::string& text) {
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
    }
}

} // namespace Terracotta2::platform
