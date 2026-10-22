#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <stdlib.h>

#include "platform.h"

#define WINDOW_CLASS_NAME L"openkickEditor_v1"

enum { RefreshTimer = 1, RefreshIntervalMs = 16 };

struct PlatformView {
    HWND window;
    Editor* editor;
    float scale;
    bool tracking;
};

static ATOM windowClass;

static HINSTANCE moduleInstance(void) {
    HMODULE module = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void*)&moduleInstance, &module);
    return module;
}

static EditorModifiers modifiersFromKeys(WPARAM keys) {
    return (keys & MK_SHIFT) ? EditorModifierFine : EditorModifierNone;
}

static void paint(PlatformView* view) {
    PAINTSTRUCT paintStruct;
    HDC context = BeginPaint(view->window, &paintStruct);
    int width = 0, height = 0;
    const uint32_t* pixels = editorRender(view->editor, view->scale, &width, &height);
    if (pixels) {
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(context, 0, 0, (DWORD)width, (DWORD)height, 0, 0, 0, (UINT)height, pixels, &info,
                          DIB_RGB_COLORS);
    }
    EndPaint(view->window, &paintStruct);
}

static void trackMouseLeave(PlatformView* view) {
    if (view->tracking) {
        return;
    }
    TRACKMOUSEEVENT track = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, view->window, 0};
    view->tracking = TrackMouseEvent(&track) != 0;
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    PlatformView* view = (PlatformView*)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (!view) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    float x = (float)GET_X_LPARAM(lParam) / view->scale;
    float y = (float)GET_Y_LPARAM(lParam) / view->scale;
    switch (message) {
    case WM_TIMER:
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(view);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(window);
        editorMouseDown(view->editor, x, y, message == WM_LBUTTONDBLCLK ? 2 : 1, modifiersFromKeys(wParam));
        return 0;
    case WM_MOUSEMOVE:
        trackMouseLeave(view);
        if (wParam & MK_LBUTTON) {
            editorMouseDrag(view->editor, x, y, modifiersFromKeys(wParam));
        } else {
            editorMouseMove(view->editor, x, y);
        }
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        editorMouseUp(view->editor);
        return 0;
    case WM_MOUSELEAVE:
        view->tracking = false;
        editorMouseExit(view->editor);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        editorMouseWheel(view->editor, (float)point.x / view->scale, (float)point.y / view->scale, delta,
                         modifiersFromKeys(GET_KEYSTATE_WPARAM(wParam)));
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, RefreshTimer);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

static bool registerWindowClass(void) {
    if (windowClass) {
        return true;
    }
    WNDCLASSW description = {0};
    description.style = CS_DBLCLKS;
    description.lpfnWndProc = windowProcedure;
    description.hInstance = moduleInstance();
    description.hCursor = LoadCursor(NULL, IDC_ARROW);
    description.lpszClassName = WINDOW_CLASS_NAME;
    windowClass = RegisterClassW(&description);
    return windowClass != 0;
}

PlatformView* platformViewAttach(void* parent, Editor* editor, float scale) {
    if (!registerWindowClass()) {
        return NULL;
    }
    PlatformView* view = calloc(1, sizeof(PlatformView));
    if (!view) {
        return NULL;
    }
    view->editor = editor;
    view->scale = scale > 0.0f ? scale : 1.0f;
    int width = (int)(EditorWidth * view->scale + 0.5f);
    int height = (int)(EditorHeight * view->scale + 0.5f);
    view->window = CreateWindowExW(0, WINDOW_CLASS_NAME, L"openkick", WS_CHILD | WS_VISIBLE, 0, 0, width, height,
                                   (HWND)parent, NULL, moduleInstance(), view);
    if (!view->window) {
        free(view);
        return NULL;
    }
    SetTimer(view->window, RefreshTimer, RefreshIntervalMs, NULL);
    return view;
}

void platformViewSetScale(PlatformView* view, float scale) {
    view->scale = scale;
    SetWindowPos(view->window, NULL, 0, 0, (int)(EditorWidth * scale + 0.5f), (int)(EditorHeight * scale + 0.5f),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(view->window, NULL, FALSE);
}

void platformViewDetach(PlatformView* view) {
    DestroyWindow(view->window);
    free(view);
}

void platformModuleExit(void) {
    if (windowClass) {
        UnregisterClassW(WINDOW_CLASS_NAME, moduleInstance());
        windowClass = 0;
    }
}
