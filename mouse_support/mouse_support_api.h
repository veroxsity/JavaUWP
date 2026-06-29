#ifndef MOUSE_SUPPORT_API_H
#define MOUSE_SUPPORT_API_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MOUSE_SUPPORT_EXPORTS)
#define MOUSE_SUPPORT_API __declspec(dllexport)
#else
#define MOUSE_SUPPORT_API __declspec(dllimport)
#endif

#define MOUSE_SUPPORT_MAX_BUTTONS 64

typedef struct MouseSupportButtonEvent {
    int button;
    int action;
} MouseSupportButtonEvent;

typedef struct MouseSupportFrame {
    double dx;
    double dy;
    double wheel;
    int hasAbsolute;
    int absoluteWindow;
    double absX;
    double absY;
    int buttonCount;
    MouseSupportButtonEvent buttons[MOUSE_SUPPORT_MAX_BUTTONS];
} MouseSupportFrame;

typedef struct MouseSupportHostState {
    int windowWidth;
    int windowHeight;
    int menuWidth;
    int menuHeight;
    int cursorMode;
    double menuCursorX;
    double menuCursorY;
} MouseSupportHostState;

MOUSE_SUPPORT_API void MouseSupport_Init(void);
MOUSE_SUPPORT_API void MouseSupport_Shutdown(void);
MOUSE_SUPPORT_API int MouseSupport_IsRunning(void);
MOUSE_SUPPORT_API int MouseSupport_PollFrame(MouseSupportFrame* out);
MOUSE_SUPPORT_API void MouseSupport_SetHostState(const MouseSupportHostState* state);
MOUSE_SUPPORT_API void MouseSupport_SendCursorSync(double x, double y);
MOUSE_SUPPORT_API void MouseSupport_SendWindowCursorSync(double x, double y);
MOUSE_SUPPORT_API void MouseSupport_UpdateOverlay(double menuCursorX, double menuCursorY, int visible);
MOUSE_SUPPORT_API unsigned int MouseSupport_LastActivityTickMs(void);
MOUSE_SUPPORT_API double MouseSupport_SmoothingMs(void);

#ifdef __cplusplus
}
#endif

#endif
