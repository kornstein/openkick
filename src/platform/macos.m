#import <Cocoa/Cocoa.h>

#include "platform.h"

#define openkickEditorView openkickEditorView_v1

static EditorModifiers modifiersFromEvent(NSEvent* event) {
    return (event.modifierFlags & NSEventModifierFlagShift) ? EditorModifierFine : EditorModifierNone;
}

@interface openkickEditorView : NSView
- (instancetype)initWithEditor:(Editor*)editor;
- (void)invalidate;
@end

@implementation openkickEditorView {
    Editor* _editor;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
    CGColorSpaceRef _colorSpace;
}

- (instancetype)initWithEditor:(Editor*)editor {
    self = [super initWithFrame:NSMakeRect(0, 0, EditorWidth, EditorHeight)];
    if (self) {
        _editor = editor;
        _colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0
                                         target:self
                                       selector:@selector(refresh:)
                                       userInfo:nil
                                        repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    }
    return self;
}

- (void)dealloc {
    CGColorSpaceRelease(_colorSpace);
}

- (void)invalidate {
    [_timer invalidate];
    _timer = nil;
    _editor = NULL;
}

- (void)refresh:(NSTimer*)timer {
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)isOpaque {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!_editor) {
        return;
    }
    float scale = self.window ? (float)self.window.backingScaleFactor : 2.0f;
    int width = 0, height = 0;
    const uint32_t* pixels = editorRender(_editor, scale, &width, &height);
    if (!pixels) {
        return;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, (size_t)width * (size_t)height * 4, NULL);
    CGImageRef image = CGImageCreate((size_t)width, (size_t)height, 8, 32, (size_t)width * 4, _colorSpace,
                                     kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, provider, NULL, false,
                                     kCGRenderingIntentDefault);
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, EditorHeight);
    CGContextScaleCTM(context, 1, -1);
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    CGContextDrawImage(context, CGRectMake(0, 0, EditorWidth, EditorHeight), image);
    CGContextRestoreGState(context);
    CGImageRelease(image);
    CGDataProviderRelease(provider);
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
    }
    NSTrackingAreaOptions options =
        NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (NSPoint)locationOfEvent:(NSEvent*)event {
    return [self convertPoint:event.locationInWindow fromView:nil];
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint point = [self locationOfEvent:event];
    if (_editor) {
        editorMouseDown(_editor, (float)point.x, (float)point.y, (int)event.clickCount, modifiersFromEvent(event));
    }
}

- (void)mouseDragged:(NSEvent*)event {
    NSPoint point = [self locationOfEvent:event];
    if (_editor) {
        editorMouseDrag(_editor, (float)point.x, (float)point.y, modifiersFromEvent(event));
    }
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    if (_editor) {
        editorMouseUp(_editor);
    }
}

- (void)mouseMoved:(NSEvent*)event {
    NSPoint point = [self locationOfEvent:event];
    if (_editor) {
        editorMouseMove(_editor, (float)point.x, (float)point.y);
    }
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    if (_editor) {
        editorMouseExit(_editor);
    }
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_editor || event.scrollingDeltaY == 0) {
        return;
    }
    NSPoint point = [self locationOfEvent:event];
    double delta = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 10.0 : event.scrollingDeltaY;
    editorMouseWheel(_editor, (float)point.x, (float)point.y, (float)delta, modifiersFromEvent(event));
}

@end

struct PlatformView {
    void* view;
};

PlatformView* platformViewAttach(void* parent, Editor* editor, float scale) {
    (void)scale;
    PlatformView* platformView = calloc(1, sizeof(PlatformView));
    if (!platformView) {
        return NULL;
    }
    openkickEditorView* view = [[openkickEditorView alloc] initWithEditor:editor];
    [(__bridge NSView*)parent addSubview:view];
    platformView->view = (__bridge_retained void*)view;
    return platformView;
}

void platformViewSetScale(PlatformView* platformView, float scale) {
    (void)platformView;
    (void)scale;
}

void platformViewDetach(PlatformView* platformView) {
    openkickEditorView* view = (__bridge_transfer openkickEditorView*)platformView->view;
    [view invalidate];
    [view removeFromSuperview];
    free(platformView);
}

void platformModuleExit(void) {}
