#include "imgui.h"
#include <X11/X.h>
#include <X11/Xutil.h>
#include <bits/types/struct_timeval.h>
#include <cstdint>
#include <sys/select.h>
#ifndef IMGUI_DISABLE
#include "imgui_impl_x11.h"

extern "C" {
	#include <X11/Xlib.h>
	#include <X11/cursorfont.h>
	#include <X11/keysym.h>
	#include <sys/time.h>
}
// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2024-02-17: Backend: First parts of implementing mouse support to X11 backend

struct TimingData {
	unsigned long long time;
	unsigned long long ticks_per_second;
};

struct ImGui_ImplX11_Data
{
	Window                wnd;
	Display               *dpy;

    ImGuiMouseCursor      LastMouseCursor;
	uint8_t               scroll_speed;

	TimingData            time;
	unsigned int          mod_flags;
};



static ImGui_ImplX11_Data* ImGui_ImplX11_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplX11_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

IMGUI_IMPL_API void ImGui_ImplX11_Init(void *window, void *display)
{
	ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

	ImGui_ImplX11_Data *bd = IM_NEW(ImGui_ImplX11_Data)();
	io.BackendPlatformUserData = (void*)bd;
	io.BackendPlatformName = "imgui_impl_x11";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport; // We can call io.AddMouseViewportEvent() with correct data (optional)

	bd->wnd = *(Window*)window;
	bd->dpy = (Display *)display;
	bd->scroll_speed = 1;
	bd->mod_flags = 0;

    struct timeval start_time;
	gettimeofday(&start_time, NULL);

	bd->time.ticks_per_second = 1000000;
	bd->time.time = (unsigned long long)start_time.tv_sec * bd->time.ticks_per_second + (unsigned long long)start_time.tv_usec;

	ImGuiViewport *main_viewport = ImGui::GetMainViewport();
	main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (void *)bd->wnd;
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
//		ImGui_ImplX11_InitPlatformInterface(platform_has_own_dc);
	}
}

void ImGui_ImplX11_Shutdown() {
	ImGui_ImplX11_Data *bd = ImGui_ImplX11_GetBackendData();
	ImGuiIO &io = ImGui::GetIO();
}

static bool ImGui_ImplX11_UpdateMouseCursor() {
	ImGuiIO &io = ImGui::GetIO();
	ImGui_ImplX11_Data *bd = ImGui_ImplX11_GetBackendData();
	Cursor x11_cursor;
	//if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange);

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        // In xlib we basically create an empty pixmap and bind it as cursor to make it invisible
        XColor color = { 0 };
		const char data[] = { 0 };
		Pixmap pixmap = XCreateBitmapFromData(bd->dpy, (Window)bd->wnd, data, 1, 1);
		x11_cursor = XCreatePixmapCursor(bd->dpy, pixmap, pixmap, &color, &color, 0, 0);

		XDefineCursor(bd->dpy, (Window)bd->wnd, x11_cursor);
		XFreeCursor(bd->dpy, x11_cursor);
		XFreePixmap(bd->dpy, pixmap);
    }
    else
    {
        // Show OS mouse cursor
        switch (imgui_cursor)
        {
        case ImGuiMouseCursor_Arrow:        x11_cursor = XCreateFontCursor(bd->dpy, XC_arrow); break;
        case ImGuiMouseCursor_TextInput:    x11_cursor = XCreateFontCursor(bd->dpy, XC_xterm); break;
        case ImGuiMouseCursor_ResizeAll:    x11_cursor = XCreateFontCursor(bd->dpy, XC_fleur); break;
        case ImGuiMouseCursor_ResizeEW:     x11_cursor = XCreateFontCursor(bd->dpy, XC_sb_h_double_arrow); break;
        case ImGuiMouseCursor_ResizeNS:     x11_cursor = XCreateFontCursor(bd->dpy, XC_sb_v_double_arrow); break;
        case ImGuiMouseCursor_ResizeNESW:   x11_cursor = XCreateFontCursor(bd->dpy, XC_bottom_left_corner); break;
        case ImGuiMouseCursor_ResizeNWSE:   x11_cursor = XCreateFontCursor(bd->dpy, XC_bottom_right_corner); break;
        case ImGuiMouseCursor_Hand:         x11_cursor = XCreateFontCursor(bd->dpy, XC_hand2); break;
        case ImGuiMouseCursor_NotAllowed:   x11_cursor = XCreateFontCursor(bd->dpy, XC_X_cursor); break;
		default: x11_cursor = XCreateFontCursor(bd->dpy, XC_left_ptr); break;
        }
		XDefineCursor(bd->dpy, (Window)bd->wnd, x11_cursor);
		XFreeCursor(bd->dpy, x11_cursor);
    }
    return true;
}

void ImGui_ImplX11_NewFrame()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplX11_Data* bd = ImGui_ImplX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplX11_Init()?");

    // Setup display size (every frame to accommodate for window resizing)
	XWindowAttributes attr;
	XGetWindowAttributes (bd->dpy, bd->wnd, &attr);

    io.DisplaySize = ImVec2((float)(attr.width), (float)(attr.height));
    //if (bd->WantUpdateMonitors)
    //    ImGui_ImplWin32_UpdateMonitors();

    // Setup time step
    struct timeval now;
	gettimeofday(&now, NULL);

	int64_t microsec = (unsigned long long)now.tv_sec * bd->time.ticks_per_second + (unsigned long long)now.tv_usec;
	io.DeltaTime = (float)(microsec - bd->time.time) / bd->time.ticks_per_second;
	bd->time.time = microsec;

    // Update OS mouse cursor with the cursor requested by imgui
    ImGuiMouseCursor mouse_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (bd->LastMouseCursor != mouse_cursor)
    {
        bd->LastMouseCursor = mouse_cursor;
        ImGui_ImplX11_UpdateMouseCursor();
    }
}

static ImGuiKey ImGui_ImplX11_KeySymToImGuiKey(KeySym key)
{
    switch (key)
    {
        case XK_Tab: return ImGuiKey_Tab;
        case XK_Left: return ImGuiKey_LeftArrow;
        case XK_Right: return ImGuiKey_RightArrow;
        case XK_Up: return ImGuiKey_UpArrow;
        case XK_Down: return ImGuiKey_DownArrow;
        case XK_Prior: return ImGuiKey_PageUp;
        case XK_Next: return ImGuiKey_PageDown;
        case XK_Home: return ImGuiKey_Home;
        case XK_End: return ImGuiKey_End;
        case XK_Insert: return ImGuiKey_Insert;
        case XK_Delete: return ImGuiKey_Delete;
        case XK_BackSpace: return ImGuiKey_Backspace;
        case XK_space: return ImGuiKey_Space;
        case XK_Return: return ImGuiKey_Enter;
        case XK_Escape: return ImGuiKey_Escape;
        case XK_apostrophe: return ImGuiKey_Apostrophe;
        case XK_comma: return ImGuiKey_Comma;
        case XK_minus: return ImGuiKey_Minus;
        case XK_period: return ImGuiKey_Period;
        case XK_slash: return ImGuiKey_Slash;
        case XK_semicolon: return ImGuiKey_Semicolon;
        case XK_equal: return ImGuiKey_Equal;
        case XK_bracketleft: return ImGuiKey_LeftBracket;
        case XK_backslash: return ImGuiKey_Backslash;
        case XK_bracketright: return ImGuiKey_RightBracket;
        case XK_grave: return ImGuiKey_GraveAccent;
        case XK_Caps_Lock: return ImGuiKey_CapsLock;
        case XK_Scroll_Lock: return ImGuiKey_ScrollLock;
        case XK_Num_Lock: return ImGuiKey_NumLock;
        case XK_Print: return ImGuiKey_PrintScreen;
        case XK_Pause: return ImGuiKey_Pause;
        case XK_KP_0: return ImGuiKey_Keypad0;
        case XK_KP_1: return ImGuiKey_Keypad1;
        case XK_KP_2: return ImGuiKey_Keypad2;
        case XK_KP_3: return ImGuiKey_Keypad3;
        case XK_KP_4: return ImGuiKey_Keypad4;
        case XK_KP_5: return ImGuiKey_Keypad5;
        case XK_KP_6: return ImGuiKey_Keypad6;
        case XK_KP_7: return ImGuiKey_Keypad7;
        case XK_KP_8: return ImGuiKey_Keypad8;
        case XK_KP_9: return ImGuiKey_Keypad9;
        case XK_KP_Decimal: return ImGuiKey_KeypadDecimal;
        case XK_KP_Divide: return ImGuiKey_KeypadDivide;
        case XK_KP_Multiply: return ImGuiKey_KeypadMultiply;
        case XK_KP_Subtract: return ImGuiKey_KeypadSubtract;
        case XK_KP_Add: return ImGuiKey_KeypadAdd;
        case XK_KP_Enter: return ImGuiKey_KeypadEnter;
        case XK_Shift_L: return ImGuiKey_LeftShift;
        case XK_Control_L: return ImGuiKey_LeftCtrl;
        case XK_Alt_L: return ImGuiKey_LeftAlt;
        case XK_Super_L: return ImGuiKey_LeftSuper;
        case XK_Shift_R: return ImGuiKey_RightShift;
        case XK_Control_R: return ImGuiKey_RightCtrl;
        case XK_Alt_R: return ImGuiKey_RightAlt;
        case XK_Super_R: return ImGuiKey_RightSuper;
        case XK_Menu: return ImGuiKey_Menu;
        case XK_0: return ImGuiKey_0;
        case XK_1: return ImGuiKey_1;
        case XK_2: return ImGuiKey_2;
        case XK_3: return ImGuiKey_3;
        case XK_4: return ImGuiKey_4;
        case XK_5: return ImGuiKey_5;
        case XK_6: return ImGuiKey_6;
        case XK_7: return ImGuiKey_7;
        case XK_8: return ImGuiKey_8;
        case XK_9: return ImGuiKey_9;
        case XK_A: case XK_a: return ImGuiKey_A;
        case XK_B: case XK_b: return ImGuiKey_B;
        case XK_C: case XK_c: return ImGuiKey_C;
        case XK_D: case XK_d: return ImGuiKey_D;
        case XK_E: case XK_e: return ImGuiKey_E;
        case XK_F: case XK_f: return ImGuiKey_F;
        case XK_G: case XK_g: return ImGuiKey_G;
        case XK_H: case XK_h: return ImGuiKey_H;
        case XK_I: case XK_i: return ImGuiKey_I;
        case XK_J: case XK_j: return ImGuiKey_J;
        case XK_K: case XK_k: return ImGuiKey_K;
        case XK_L: case XK_l: return ImGuiKey_L;
        case XK_M: case XK_m: return ImGuiKey_M;
        case XK_N: case XK_n: return ImGuiKey_N;
        case XK_O: case XK_o: return ImGuiKey_O;
        case XK_P: case XK_p: return ImGuiKey_P;
        case XK_Q: case XK_q: return ImGuiKey_Q;
        case XK_R: case XK_r: return ImGuiKey_R;
        case XK_S: case XK_s: return ImGuiKey_S;
        case XK_T: case XK_t: return ImGuiKey_T;
        case XK_U: case XK_u: return ImGuiKey_U;
        case XK_V: case XK_v: return ImGuiKey_V;
        case XK_W: case XK_w: return ImGuiKey_W;
        case XK_X: case XK_x: return ImGuiKey_X;
        case XK_Y: case XK_y: return ImGuiKey_Y;
        case XK_Z: case XK_z: return ImGuiKey_Z;
        case XK_F1: return ImGuiKey_F1;
        case XK_F2: return ImGuiKey_F2;
        case XK_F3: return ImGuiKey_F3;
        case XK_F4: return ImGuiKey_F4;
        case XK_F5: return ImGuiKey_F5;
        case XK_F6: return ImGuiKey_F6;
        case XK_F7: return ImGuiKey_F7;
        case XK_F8: return ImGuiKey_F8;
        case XK_F9: return ImGuiKey_F9;
        case XK_F10: return ImGuiKey_F10;
        case XK_F11: return ImGuiKey_F11;
        case XK_F12: return ImGuiKey_F12;
        case XK_F13: return ImGuiKey_F13;
        case XK_F14: return ImGuiKey_F14;
        case XK_F15: return ImGuiKey_F15;
        case XK_F16: return ImGuiKey_F16;
        case XK_F17: return ImGuiKey_F17;
        case XK_F18: return ImGuiKey_F18;
        case XK_F19: return ImGuiKey_F19;
        case XK_F20: return ImGuiKey_F20;
        case XK_F21: return ImGuiKey_F21;
        case XK_F22: return ImGuiKey_F22;
        case XK_F23: return ImGuiKey_F23;
        case XK_F24: return ImGuiKey_F24;
        //case XK_??: return ImGuiKey_AppBack;
        //case XK_??: return ImGuiKey_AppForward;
        default: return ImGuiKey_None;
    }
}

static void ImGui_ImplX11_UpdateKeyModifiers(const KeySym &key) {
	ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplX11_Data* bd = ImGui_ImplX11_GetBackendData();

	io.AddKeyEvent(ImGuiMod_Ctrl,  (bd->mod_flags & ControlMask) != 0);
	io.AddKeyEvent(ImGuiMod_Shift, (bd->mod_flags & ShiftMask) != 0);
	io.AddKeyEvent(ImGuiMod_Alt,   (bd->mod_flags & (Mod1Mask | Mod5Mask)) != 0);
	io.AddKeyEvent(ImGuiMod_Super, (bd->mod_flags & Mod4Mask) != 0);

}

IMGUI_IMPL_API void ImGui_ImplX11_ProcessEvent(void *event)
{
	XEvent *xevent = (XEvent *)event;
	ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplX11_Data* bd = ImGui_ImplX11_GetBackendData();

	switch (xevent->type) {
		case MotionNotify: {
			bool want_absolute_pos = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
			io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
			io.AddMousePosEvent((float)xevent->xmotion.x, (float)xevent->xmotion.y);
		} break;
		case ButtonPress:
			switch (xevent->xbutton.button ) {
				case 1:
					io.MouseDown[0] = true;
				break;
				case 2:
					io.MouseDown[2] = true;
				break;
				case 3:
					io.MouseDown[1] = true;
				break;
				case 4:
					io.AddMouseWheelEvent(0.0f, bd->scroll_speed);
				break;
				case 5:
					io.AddMouseWheelEvent(0.0f, -bd->scroll_speed);
				break;
				default:
					io.MouseDown[xevent->xbutton.button] = true;
				break;
			}
		break;
		case ButtonRelease:
			switch (xevent->xbutton.button ) {
				case 1:
					io.MouseDown[0] = false;
				break;
				case 2:
					io.MouseDown[2] = false;
				break;
				case 3:
					io.MouseDown[1] = false;
				break;
				default:
					io.MouseDown[xevent->xbutton.button] = false;
				break;
			}
		break;
		case KeyPress:
		case KeyRelease:
		{
			KeySym ks = XLookupKeysym(&xevent->xkey, 0);
			ImGuiKey imgui_key = ImGui_ImplX11_KeySymToImGuiKey(ks);
			bool is_key_down = xevent->type == KeyPress;

			if (imgui_key != ImGuiKey_None) {
				if(xevent->type == KeyPress && ks >= ' ' && ks <= '~') {
					char buffer[32];// = XKeysymToString(ks);
					XLookupString(&xevent->xkey, buffer, sizeof(buffer), &ks, NULL);
					io.AddInputCharacter((unsigned int)buffer[0]);
				} else {
					io.AddKeyEvent(imgui_key, is_key_down);
				}
			}
			// Since the xevent.xkey.state typically is not updated in time, it becomes
			// tricky to make use of it for knowing the Modifiers. So for example
			// if I press left shift we will not be able to update based on the state.
			// So instead I opted to make use of the Masks and store the state in our
			// backend data. There probably are better solutions, and it would be nice
			// to not be dependent on the backend data.
			if (imgui_key == ImGuiKey_LeftShift || imgui_key == ImGuiKey_RightShift) {
				bd->mod_flags = (bd->mod_flags & ~ShiftMask) | (is_key_down ? ShiftMask : 0);
			}
			if (imgui_key == ImGuiKey_LeftCtrl || imgui_key == ImGuiKey_RightCtrl) {
				bd->mod_flags = (bd->mod_flags & ~ControlMask) | (is_key_down ? ControlMask : 0);
			}
			if (imgui_key == ImGuiKey_LeftSuper || imgui_key == ImGuiKey_RightSuper) {
				bd->mod_flags = (bd->mod_flags & ~Mod4Mask) | (is_key_down ? Mod4Mask : 0);
			}
			if (imgui_key == ImGuiKey_LeftAlt || imgui_key == ImGuiKey_RightAlt) {
				bd->mod_flags = (bd->mod_flags & ~(Mod1Mask | Mod5Mask)) | (is_key_down ? (Mod1Mask | Mod5Mask) : 0);
			}
			ImGui_ImplX11_UpdateKeyModifiers(ks);
		}
		break;
	}
}

#endif
