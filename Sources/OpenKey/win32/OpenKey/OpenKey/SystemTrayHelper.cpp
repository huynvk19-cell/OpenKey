/*----------------------------------------------------------
OpenKey - The Cross platform Open source Vietnamese Keyboard application.

Copyright (C) 2019 Mai Vu Tuyen
Contact: maivutuyen.91@gmail.com
Github: https://github.com/tuyenvm/OpenKey
Fanpage: https://www.facebook.com/OpenKeyVN

This file is belong to the OpenKey project, Win32 version
which is released under GPL license.
You can fork, modify, improve this program. If you
redistribute your new version, it MUST be open source.
-----------------------------------------------------------*/
#include "SystemTrayHelper.h"
#include "AppDelegate.h"

#define WM_TRAYMESSAGE (WM_USER + 1)
#define TRAY_ICONUID 100

#define POPUP_VIET_ON_OFF 900
#define POPUP_SPELLING 901
#define POPUP_SMART_SWITCH 902
#define POPUP_USE_MACRO 903

#define POPUP_TELEX 910
#define POPUP_VNI 911
#define POPUP_SIMPLE_TELEX 912

#define POPUP_UNICODE 930
#define POPUP_TCVN3 931
#define POPUP_VNI_WINDOWS 932
#define POPUP_UNICODE_COMPOUND 933
#define POPUP_VN_LOCALE_1258 934

#define POPUP_CONVERT_TOOL 980
#define POPUP_QUICK_CONVERT 981

#define POPUP_MACRO_TABLE 990

#define POPUP_CONTROL_PANEL 1000
#define POPUP_ABOUT_OPENKEY 1010
#define POPUP_OPENKEY_EXIT 2000

#define MODIFY_MENU(MENU, COMMAND, DATA) ModifyMenu(MENU, COMMAND, \
											MF_BYCOMMAND | (DATA ? MF_CHECKED : MF_UNCHECKED), \
											COMMAND, \
											menuData[COMMAND]);

static HMENU popupMenu;
//static HMENU menuInputType;
static HMENU otherCode;

static NOTIFYICONDATA nid;

#define POPUP_AUTO_LAYOUT 3000
#define POPUP_RUN_ON_STARTUP 3010

//NhaKey keeps the menu tiny: everything else just works by itself
map<UINT, LPCTSTR> menuData = {
	{POPUP_VIET_ON_OFF, _T("Tiếng Việt")},
	{POPUP_AUTO_LAYOUT, _T("Tự đổi bàn phím")},
	{POPUP_RUN_ON_STARTUP, _T("Mở cùng máy tính")},
	{POPUP_OPENKEY_EXIT, _T("Thoát")},
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	static UINT taskbarCreated;

	switch (message) {
	case WM_CREATE:
		taskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));
		break;
	case WM_USER+2019: //opened again while running
		SystemTrayHelper::showMessage(_T("NhaKey đang chạy"), _T("Cứ gõ bình thường, NhaKey tự lo."));
		break;
	case WM_TRAYMESSAGE: {
		if (lParam == WM_LBUTTONUP) {
			AppDelegate::getInstance()->onToggleVietnamese();
			SystemTrayHelper::updateData();
		} else if (lParam == WM_RBUTTONDOWN) {
			POINT curPoint;
			GetCursorPos(&curPoint);
			SetForegroundWindow(hWnd);
			UINT commandId = TrackPopupMenu(
				popupMenu,
				TPM_RETURNCMD | TPM_NONOTIFY,
				curPoint.x,
				curPoint.y,
				0,
				hWnd,
				NULL
			);
			switch (commandId) {
			case POPUP_VIET_ON_OFF:
				AppDelegate::getInstance()->onToggleVietnamese();
				break;
			case POPUP_AUTO_LAYOUT:
				APP_SET_DATA(vAutoSwitchLayout, vAutoSwitchLayout ? 0 : 1);
				break;
			case POPUP_RUN_ON_STARTUP:
				APP_SET_DATA(vRunWithWindows, vRunWithWindows ? 0 : 1);
				OpenKeyHelper::registerRunOnStartup(vRunWithWindows);
				break;
			case POPUP_OPENKEY_EXIT:
				AppDelegate::getInstance()->onOpenKeyExit();
				break;
			}
			SystemTrayHelper::updateData();
		}
	}
	break;
	default:
		// if the taskbar is restarted, add the system tray icon again
		if (message == taskbarCreated) {
			Shell_NotifyIcon(NIM_ADD, &nid);
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

HWND SystemTrayHelper::createFakeWindow(const HINSTANCE & hIns) {
	//create fake window
	WNDCLASSEXW wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = 0;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hIns;
	wcex.hIcon = LoadIcon(hIns, MAKEINTRESOURCE(IDI_APP_ICON));
	wcex.hCursor = NULL;
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = APP_CLASS;
	wcex.hIconSm = NULL;
	ATOM atom = RegisterClassExW(&wcex);
	HWND hWnd = CreateWindowW(APP_CLASS, _T(""), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hIns, nullptr);
	if (!hWnd) {
		return NULL;
	}
	ShowWindow(hWnd, 0);
	UpdateWindow(hWnd);
	return hWnd;
}

void SystemTrayHelper::createPopupMenu() {
	popupMenu = CreatePopupMenu();
	AppendMenu(popupMenu, MF_CHECKED, POPUP_VIET_ON_OFF, menuData[POPUP_VIET_ON_OFF]);
	AppendMenu(popupMenu, MF_CHECKED, POPUP_AUTO_LAYOUT, menuData[POPUP_AUTO_LAYOUT]);
	AppendMenu(popupMenu, MF_CHECKED, POPUP_RUN_ON_STARTUP, menuData[POPUP_RUN_ON_STARTUP]);
	AppendMenu(popupMenu, MF_SEPARATOR, 0, 0);
	AppendMenu(popupMenu, MF_UNCHECKED, POPUP_OPENKEY_EXIT, menuData[POPUP_OPENKEY_EXIT]);
}

void SystemTrayHelper::showMessage(LPCTSTR title, LPCTSTR text) {
	NOTIFYICONDATA info = nid;
	info.uFlags = NIF_INFO;
	info.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
	lstrcpyn(info.szInfoTitle, title, ARRAYSIZE(info.szInfoTitle));
	lstrcpyn(info.szInfo, text, ARRAYSIZE(info.szInfo));
	Shell_NotifyIcon(NIM_MODIFY, &info);
}

extern bool vPausedForApp;
extern wstring vPausedAppName;

static void loadTrayIcon() {
	int icon = 0;
	if (vPausedForApp) { //Moonlight, UltraViewer...: keys go to the other computer untouched
		icon = vUseGrayIcon ? IDI_ICON_STATUS_ENG_10 : IDI_ICON_STATUS_ENG;
		wstring tip = _T("NhaKey: tạm nghỉ trong ") + vPausedAppName;
		lstrcpyn(nid.szTip, tip.c_str(), 128);
	}
	else if (vLanguage) {
		icon = vUseGrayIcon ? IDI_ICON_STATUS_VIET_10 : IDI_ICON_STATUS_VIET;
		lstrcpyn(nid.szTip, _T("NhaKey: Tiếng Việt (bấm để tắt)"), 128);
	}
	else {
		icon = vUseGrayIcon ? IDI_ICON_STATUS_ENG_10 : IDI_ICON_STATUS_ENG;
		lstrcpyn(nid.szTip, _T("NhaKey: English (bấm để gõ Tiếng Việt)"), 128);
	}
	nid.hIcon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(icon));
}

void SystemTrayHelper::updateData() {
	loadTrayIcon();
	Shell_NotifyIcon(NIM_MODIFY, &nid);

	MODIFY_MENU(popupMenu, POPUP_VIET_ON_OFF, vLanguage);
	MODIFY_MENU(popupMenu, POPUP_AUTO_LAYOUT, vAutoSwitchLayout);
	MODIFY_MENU(popupMenu, POPUP_RUN_ON_STARTUP, vRunWithWindows);
}

static HINSTANCE ins;
static int recreateCount = 0;

void SystemTrayHelper::_createSystemTrayIcon(const HINSTANCE& hIns) {
	HWND hWnd = createFakeWindow(ins);
	
	if (hWnd == NULL) { //Use timer to create
		if (recreateCount >= 5) {
			PostQuitMessage(0);
			return;
		}
		ins = hIns;
		SetTimer(NULL, 0, 1000 * 3, (TIMERPROC)&WaitToCreateFakeWindow);
		++recreateCount;
		return;
	}
	createPopupMenu();

	//create system tray
	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = hWnd;
	nid.uID = TRAY_ICONUID;
	nid.uVersion = NOTIFYICON_VERSION;
	nid.uCallbackMessage = WM_TRAYMESSAGE;
	loadTrayIcon();
	LoadString(ins, IDS_APP_TITLE, nid.szTip, 128);
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

	// Shell_NotifyIcon may fail if the system tray icon is not fully initialized
	const int maxRetries = 5;
	for (int attempt = 0; attempt < maxRetries; ++attempt) {
		if (Shell_NotifyIcon(NIM_ADD, &nid)) {
			break;
		}
		Sleep(1000);
	}
}


void CALLBACK SystemTrayHelper::WaitToCreateFakeWindow(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime) {
	_createSystemTrayIcon(ins);
	KillTimer(0, timerId);
}

void SystemTrayHelper::createSystemTrayIcon(const HINSTANCE& hIns) {
	_createSystemTrayIcon(hIns);
}

void SystemTrayHelper::removeSystemTray() {
	Shell_NotifyIcon(NIM_DELETE, &nid);
}