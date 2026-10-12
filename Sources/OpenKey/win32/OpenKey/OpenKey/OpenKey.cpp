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
#include "stdafx.h"
#include "AppDelegate.h"
#include "../../../engine/AutoSwitch.h"

#pragma comment(lib, "imm32")
#define IMC_GETOPENSTATUS 0x0005

#define MASK_SHIFT				0x01
#define MASK_CONTROL			0x02
#define MASK_ALT				0x04
#define MASK_CAPITAL			0x08
#define MASK_NUMLOCK			0x10
#define MASK_WIN				0x20
#define MASK_SCROLL				0x40

#define OTHER_CONTROL_KEY (_flag & MASK_ALT) || (_flag & MASK_CONTROL)
#define DYNA_DATA(macro, pos) (macro ? pData->macroData[pos] : pData->charData[pos])
#define EMPTY_HOTKEY 0xFE0000FE

static vector<string> _chromiumBrowser = {
	"chrome.exe", "brave.exe", "msedge.exe"
};

extern int vSendKeyStepByStep;
extern int vUseGrayIcon;
extern int vShowOnStartUp;
extern int vRunWithWindows;

static HHOOK hKeyboardHook;
static HHOOK hMouseHook;
static HWINEVENTHOOK hSystemEvent;
static KBDLLHOOKSTRUCT* keyboardData;
static MSLLHOOKSTRUCT* mouseData;
static vKeyHookState* pData;
static vector<Uint16> _syncKey;
static Uint32 _flag = 0, _lastFlag = 0, _privateFlag;
static bool _flagChanged = false, _isFlagKey;
static Uint16 _keycode;
static Uint16 _newChar, _newCharHi;

static vector<Uint16> _newCharString;
static Uint16 _newCharSize;
static bool _willSendControlKey = false;

static Uint16 _uniChar[2];
static int _i, _j, _k;
static Uint32 _tempChar;

static string macroText, macroContent;
static int _languageTemp = 0; //use for smart switch key
static vector<Byte> savedSmartSwitchKeyData; ////use for smart switch key

static bool _hasJustUsedHotKey = false;

static INPUT backspaceEvent[2];
static INPUT keyEvent[2];

//auto switch keyboard layout (QWERTY / Dvorak) and language (English / Vietnamese)
int vAutoSwitchLayout = 1;
int vAutoSwitchLanguage = 1;
static AutoSwitch _autoSwitch;
static int _screenCount = 0; //characters on screen for the current word
static bool _passedThrough = false;

//count what OpenKey types, so a whole word can be deleted and retyped later
static UINT CountedSendInput(UINT count, LPINPUT inputs, int size) {
	for (UINT i = 0; i < count; i++) {
		if (inputs[i].type != INPUT_KEYBOARD || (inputs[i].ki.dwFlags & KEYEVENTF_KEYUP))
			continue;
		WORD vk = inputs[i].ki.wVk;
		if (inputs[i].ki.dwFlags & KEYEVENTF_UNICODE) _screenCount++;
		else if (vk == VK_BACK) _screenCount--;
		else if (vk == VK_LEFT) _screenCount--; //chromium fix: shift + left selects a char that the next char replaces
		else if (vk == VK_INSERT) _autoSwitch.invalidate(); //paste mode: can not count
		else if ((vk >= '0' && vk <= 'Z') || (vk >= VK_OEM_1 && vk <= VK_OEM_8)) _screenCount++;
	}
	return SendInput(count, inputs, size);
}
static UINT RawSendInput(UINT count, LPINPUT inputs, int size) {
	return SendInput(count, inputs, size);
}
#define SendInput CountedSendInput

LRESULT CALLBACK keyboardHookProcess(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK mouseHookProcess(int nCode, WPARAM wParam, LPARAM lParam);
VOID CALLBACK winEventProcCallback(HWINEVENTHOOK hWinEventHook, DWORD dwEvent, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);

void OpenKeyFree() {
	UnhookWindowsHookEx(hMouseHook);
	UnhookWindowsHookEx(hKeyboardHook);
	UnhookWinEvent(hSystemEvent);
}

void OpenKeyInit() {
	APP_GET_DATA(vLanguage, 1);
	APP_GET_DATA(vInputType, 0);
	vFreeMark = 0;
	APP_GET_DATA(vCodeTable, 0);
	APP_GET_DATA(vCheckSpelling, 1);
	APP_GET_DATA(vUseModernOrthography, 0);
	APP_GET_DATA(vQuickTelex, 0);
	APP_GET_DATA(vSwitchKeyStatus, 0x7A000206);
	APP_GET_DATA(vRestoreIfWrongSpelling, 1);
	APP_GET_DATA(vFixRecommendBrowser, 1);
	APP_GET_DATA(vUseMacro, 1);
	APP_GET_DATA(vUseMacroInEnglishMode, 0);
	APP_GET_DATA(vAutoCapsMacro, 0);
	APP_GET_DATA(vSendKeyStepByStep, 1);
	APP_GET_DATA(vUseGrayIcon, 0);
	APP_GET_DATA(vShowOnStartUp, 1);
	APP_GET_DATA(vRunWithWindows, 1);
	OpenKeyHelper::registerRunOnStartup(vRunWithWindows);
	APP_GET_DATA(vUseSmartSwitchKey, 1);
	APP_GET_DATA(vUpperCaseFirstChar, 0);
	APP_GET_DATA(vAllowConsonantZFWJ, 0);
	APP_GET_DATA(vTempOffSpelling, 0);
	APP_GET_DATA(vQuickStartConsonant, 0);
	APP_GET_DATA(vQuickEndConsonant, 0);
	APP_GET_DATA(vSupportMetroApp, 0);
	APP_GET_DATA(vRunAsAdmin, 0);
	APP_GET_DATA(vCreateDesktopShortcut, 0);
	APP_GET_DATA(vCheckNewVersion, 0);
	APP_GET_DATA(vRememberCode, 1);
	APP_GET_DATA(vOtherLanguage, 1);
	APP_GET_DATA(vTempOffOpenKey, 0);
	APP_GET_DATA(vFixChromiumBrowser, 0);
	APP_GET_DATA(vAutoSwitchLayout, 1);
	APP_GET_DATA(vAutoSwitchLanguage, 1);

	//NhaKey: one simple setup that just works, nothing to configure
	vInputType = 0;              //Telex
	vCodeTable = 0;              //Unicode
	vCheckSpelling = 1;
	vRestoreIfWrongSpelling = 1; //English words stay English
	vSendKeyStepByStep = 1;      //needed to fix words automatically
	vUseSmartSwitchKey = 0;      //language is detected per word instead
	vRememberCode = 0;
	vUseMacro = 0;
	vTempOffSpelling = 0;
	vTempOffOpenKey = 0;
	vShowOnStartUp = 0;
	vCheckNewVersion = 0;
	vAutoSwitchLanguage = 1;

	//init convert tool
	APP_GET_DATA(convertToolDontAlertWhenCompleted, 0);
	APP_GET_DATA(convertToolToAllCaps, 0);
	APP_GET_DATA(convertToolToAllNonCaps, 0);
	APP_GET_DATA(convertToolToCapsFirstLetter, 0);
	APP_GET_DATA(convertToolToCapsEachWord, 0);
	APP_GET_DATA(convertToolRemoveMark, 0);
	APP_GET_DATA(convertToolFromCode, 0);
	APP_GET_DATA(convertToolToCode, 0);
	APP_GET_DATA(convertToolHotKey, EMPTY_HOTKEY);
	if (convertToolHotKey == 0) {
		convertToolHotKey = EMPTY_HOTKEY;
	}

	pData = (vKeyHookState*)vKeyInit();

	//pre-create back key
	backspaceEvent[0].type = INPUT_KEYBOARD;
	backspaceEvent[0].ki.dwFlags = 0;
	backspaceEvent[0].ki.wVk = VK_BACK;
	backspaceEvent[0].ki.wScan = 0;
	backspaceEvent[0].ki.time = 0;
	backspaceEvent[0].ki.dwExtraInfo = 1;

	backspaceEvent[1].type = INPUT_KEYBOARD;
	backspaceEvent[1].ki.dwFlags = KEYEVENTF_KEYUP;
	backspaceEvent[1].ki.wVk = VK_BACK;
	backspaceEvent[1].ki.wScan = 0;
	backspaceEvent[1].ki.time = 0;
	backspaceEvent[1].ki.dwExtraInfo = 1;

	//get key state
	_flag = 0;
	if (GetKeyState(VK_LSHIFT) < 0 || GetKeyState(VK_RSHIFT) < 0) _flag |= MASK_SHIFT;
	if (GetKeyState(VK_LCONTROL) < 0 || GetKeyState(VK_RCONTROL) < 0) _flag |= MASK_CONTROL;
	if (GetKeyState(VK_LMENU) < 0 || GetKeyState(VK_RMENU) < 0) _flag |= MASK_ALT;
	if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) _flag |= MASK_WIN;
	if (GetKeyState(VK_NUMLOCK) < 0) _flag |= MASK_NUMLOCK;
	if (GetKeyState(VK_CAPITAL) == 1) _flag |= MASK_CAPITAL;
	if (GetKeyState(VK_SCROLL) < 0) _flag |= MASK_SCROLL;

	//init and load macro data
	DWORD macroDataSize;
	BYTE* macroData = OpenKeyHelper::getRegBinary(_T("macroData"), macroDataSize);
	initMacroMap((Byte*)macroData, (int)macroDataSize);

	//init and load smart switch key data
	DWORD smartSwitchKeySize;
	BYTE* data = OpenKeyHelper::getRegBinary(_T("smartSwitchKey"), smartSwitchKeySize);
	initSmartSwitchKey((Byte*)data, (int)smartSwitchKeySize);

	//init hook
	HINSTANCE hInstance = GetModuleHandle(NULL);
	hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHookProcess, hInstance, 0);
	hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, mouseHookProcess, hInstance, 0);
	hSystemEvent = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, winEventProcCallback, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void saveSmartSwitchKeyData() {
	getSmartSwitchKeySaveData(savedSmartSwitchKeyData);
	OpenKeyHelper::setRegBinary(_T("smartSwitchKey"), savedSmartSwitchKeyData.data(), (int)savedSmartSwitchKeyData.size());
}

static void InsertKeyLength(const Uint8& len) {
	_syncKey.push_back(len);
}

static inline void prepareKeyEvent(INPUT& input, const Uint16& keycode, const bool& isPress, const DWORD& flag=0) {
	input.type = INPUT_KEYBOARD;
	input.ki.dwFlags = isPress ? flag : flag|KEYEVENTF_KEYUP;
	input.ki.wVk = keycode;
	input.ki.wScan = 0;
	input.ki.time = 0;
	input.ki.dwExtraInfo = 1;
}

static inline void prepareUnicodeEvent(INPUT& input, const Uint16& unicode, const bool& isPress) {
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = 0;
	input.ki.wScan = unicode;
	input.ki.time = 0;
	input.ki.dwFlags = (isPress ? 0 : KEYEVENTF_KEYUP) | KEYEVENTF_UNICODE;
	input.ki.dwExtraInfo = 1;
}

static void SendCombineKey(const Uint16& key1, const Uint16& key2, const DWORD& flagKey1=0, const DWORD& flagKey2 = 0) {
	prepareKeyEvent(keyEvent[0], key1, true, flagKey1);
	SendInput(1, keyEvent, sizeof(INPUT));

	prepareKeyEvent(keyEvent[0], key2, true, flagKey2);
	prepareKeyEvent(keyEvent[1], key2, false, flagKey2);
	SendInput(2, keyEvent, sizeof(INPUT));

	prepareKeyEvent(keyEvent[0], key1, false, flagKey1);
	SendInput(1, keyEvent, sizeof(INPUT));
}

static void SendKeyCode(Uint32 data) {
	_newChar = (Uint16)data;
	if (!(data & CHAR_CODE_MASK)) {
		if (IS_DOUBLE_CODE(vCodeTable)) //VNI
			InsertKeyLength(1);

		_newChar = keyCodeToCharacter(data);
		if (_newChar == 0) {
			_newChar = (Uint16)data;
			prepareKeyEvent(keyEvent[0], _newChar, true);
			prepareKeyEvent(keyEvent[1], _newChar, false);
			SendInput(2, keyEvent, sizeof(INPUT));
		} else {
			prepareUnicodeEvent(keyEvent[0], _newChar, true);
			prepareUnicodeEvent(keyEvent[1], _newChar, false);
			SendInput(2, keyEvent, sizeof(INPUT));
		}
	} else {
		if (vCodeTable == 0) { //unicode 2 bytes code
			prepareUnicodeEvent(keyEvent[0], _newChar, true);
			prepareUnicodeEvent(keyEvent[1], _newChar, false);
			SendInput(2, keyEvent, sizeof(INPUT));
		} else if (vCodeTable == 1 || vCodeTable == 2 || vCodeTable == 4) { //others such as VNI Windows, TCVN3: 1 byte code
			_newCharHi = HIBYTE(_newChar);
			_newChar = LOBYTE(_newChar);

			prepareUnicodeEvent(keyEvent[0], _newChar, true);
			prepareUnicodeEvent(keyEvent[1], _newChar, false);
			SendInput(2, keyEvent, sizeof(INPUT));

			if (_newCharHi > 32) {
				if (vCodeTable == 2) //VNI
					InsertKeyLength(2);
				prepareUnicodeEvent(keyEvent[0], _newCharHi, true);
				prepareUnicodeEvent(keyEvent[1], _newCharHi, false);
				SendInput(2, keyEvent, sizeof(INPUT));
			} else {
				if (vCodeTable == 2) //VNI
					InsertKeyLength(1);
			}
		} else if (vCodeTable == 3) { //Unicode Compound
			_newCharHi = (_newChar >> 13);
			_newChar &= 0x1FFF;
			_uniChar[0] = _newChar;
			_uniChar[1] = _newCharHi > 0 ? (_unicodeCompoundMark[_newCharHi - 1]) : 0;
			InsertKeyLength(_newCharHi > 0 ? 2 : 1);
			prepareUnicodeEvent(keyEvent[0], _uniChar[0], true);
			prepareUnicodeEvent(keyEvent[1], _uniChar[0], false);
			SendInput(2, keyEvent, sizeof(INPUT));
			if (_newCharHi > 0) {
				prepareUnicodeEvent(keyEvent[0], _uniChar[1], true);
				prepareUnicodeEvent(keyEvent[1], _uniChar[1], false);
				SendInput(2, keyEvent, sizeof(INPUT));
			}
		}
	}
}

static void SendBackspace() {
	SendInput(2, backspaceEvent, sizeof(INPUT));
	if (vSupportMetroApp && OpenKeyHelper::getLastAppExecuteName().compare("ApplicationFrameHost.exe") == 0) {//Metro App
		SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0L);
		SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0L);
	}
	if (IS_DOUBLE_CODE(vCodeTable)) { //VNI or Unicode Compound
		if (_syncKey.back() > 1) {
			/*if (!(vCodeTable == 3 && containUnicodeCompoundApp(FRONT_APP))) {
				SendInput(2, backspaceEvent, sizeof(INPUT));
			}*/
			SendInput(2, backspaceEvent, sizeof(INPUT));
			if (vSupportMetroApp && OpenKeyHelper::getLastAppExecuteName().compare("ApplicationFrameHost.exe") == 0) {//Metro App
				SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0L);
				SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0L);
			}
		}
		_syncKey.pop_back();
	}
}

static void SendEmptyCharacter() {
	if (IS_DOUBLE_CODE(vCodeTable)) //VNI or Unicode Compound
		InsertKeyLength(1);

	_newChar = 0x202F; //empty char

	prepareUnicodeEvent(keyEvent[0], _newChar, true);
	prepareUnicodeEvent(keyEvent[1], _newChar, false);
	SendInput(2, keyEvent, sizeof(INPUT));
}

static void SendNewCharString(const bool& dataFromMacro = false) {
	_j = 0;
	_newCharSize = dataFromMacro ? (Uint16)pData->macroData.size() : pData->newCharCount;
	if (_newCharString.size() < _newCharSize) {
		_newCharString.resize(_newCharSize);
	}
	_willSendControlKey = false;
	
	if (_newCharSize > 0) {
		for (_k = dataFromMacro ? 0 : pData->newCharCount - 1;
			dataFromMacro ? _k < pData->macroData.size() : _k >= 0;
			dataFromMacro ? _k++ : _k--) {

			_tempChar = DYNA_DATA(dataFromMacro, _k);
			if (_tempChar & PURE_CHARACTER_MASK) {
				_newCharString[_j++] = _tempChar;
				if (IS_DOUBLE_CODE(vCodeTable)) {
					InsertKeyLength(1);
				}
			} else if (!(_tempChar & CHAR_CODE_MASK)) {
				if (IS_DOUBLE_CODE(vCodeTable)) //VNI
					InsertKeyLength(1);
				_newCharString[_j++] = keyCodeToCharacter(_tempChar);
			} else {
				_newChar = _tempChar;
				if (vCodeTable == 0) {  //unicode 2 bytes code
					_newCharString[_j++] = _newChar;
				} else if (vCodeTable == 1 || vCodeTable == 2 || vCodeTable == 4) { //others such as VNI Windows, TCVN3: 1 byte code
					_newCharHi = HIBYTE(_newChar);
					_newChar = LOBYTE(_newChar);
					_newCharString[_j++] = _newChar;

					if (_newCharHi > 32) {
						if (vCodeTable == 2) //VNI
							InsertKeyLength(2);
						_newCharString[_j++] = _newCharHi;
						_newCharSize++;
					}
					else {
						if (vCodeTable == 2) //VNI
							InsertKeyLength(1);
					}
				} else if (vCodeTable == 3) { //Unicode Compound
					_newCharHi = (_newChar >> 13);
					_newChar &= 0x1FFF;

					InsertKeyLength(_newCharHi > 0 ? 2 : 1);
					_newCharString[_j++] = _newChar;
					if (_newCharHi > 0) {
						_newCharSize++;
						_newCharString[_j++] = _unicodeCompoundMark[_newCharHi - 1];
					}

				}
			}
		}//end for
	}

	if (pData->code == vRestore || pData->code == vRestoreAndStartNewSession) { //if is restore
		if (keyCodeToCharacter(_keycode) != 0) {
			_newCharSize++;
			_newCharString[_j++] = keyCodeToCharacter(_keycode | ((_flag & MASK_SHIFT) || (_flag & MASK_CAPITAL) ? CAPS_MASK : 0));
		} else {
			_willSendControlKey = true;
		}
	}
	if (pData->code == vRestoreAndStartNewSession) {
		startNewSession();
	}

	OpenKeyHelper::setClipboardText((LPCTSTR)_newCharString.data(), _newCharSize + 1, CF_UNICODETEXT);

	//Send shift + insert
	SendCombineKey(KEY_LEFT_SHIFT, VK_INSERT, 0, KEYEVENTF_EXTENDEDKEY);
	
	//the case when hCode is vRestore or vRestoreAndStartNewSession,
	//the word is invalid and last key is control key such as TAB, LEFT ARROW, RIGHT ARROW,...
	if (_willSendControlKey) {
		SendKeyCode(_keycode);
	}
}

bool checkHotKey(int hotKeyData, bool checkKeyCode = true) {
	if ((hotKeyData & (~0x8000)) == EMPTY_HOTKEY)
		return false;
	if (HAS_CONTROL(hotKeyData) ^ GET_BOOL(_lastFlag & MASK_CONTROL))
		return false;
	if (HAS_OPTION(hotKeyData) ^ GET_BOOL(_lastFlag & MASK_ALT))
		return false;
	if (HAS_COMMAND(hotKeyData) ^ GET_BOOL(_lastFlag & MASK_WIN))
		return false;
	if (HAS_SHIFT(hotKeyData) ^ GET_BOOL(_lastFlag & MASK_SHIFT))
		return false;
	if (checkKeyCode) {
		if (GET_SWITCH_KEY(hotKeyData) != _keycode)
			return false;
	}
	return true;
}

void switchLanguage() {
	if (vLanguage == 0)
		vLanguage = 1;
	else
		vLanguage = 0;
	if (HAS_BEEP(vSwitchKeyStatus))
		MessageBeep(MB_OK);
	AppDelegate::getInstance()->onInputMethodChangedFromHotKey();
	if (vUseSmartSwitchKey) {
		setAppInputMethodStatus(OpenKeyHelper::getFrontMostAppExecuteName(), vLanguage | (vCodeTable << 1));
		saveSmartSwitchKeyData();
	}
	startNewSession();
}

static void SendPureCharacter(const Uint16& ch) {
	if (ch < 128)
		SendKeyCode(ch);
	else {
		prepareUnicodeEvent(keyEvent[0], ch, true);
		prepareUnicodeEvent(keyEvent[1], ch, false);
		SendInput(2, keyEvent, sizeof(INPUT));
		if (IS_DOUBLE_CODE(vCodeTable)) {
			InsertKeyLength(1);
		}
	}
}

static void handleMacro() {
	//fix autocomplete
	if (vFixRecommendBrowser) {
		SendEmptyCharacter();
		pData->backspaceCount++;
	}

	//send backspace
	if (pData->backspaceCount > 0) {
		for (int i = 0; i < pData->backspaceCount; i++) {
			SendBackspace();
		}
	}
	//send real data
	if (!vSendKeyStepByStep) {
		SendNewCharString(true);
	} else {
		for (int i = 0; i < pData->macroData.size(); i++) {
			if (pData->macroData[i] & PURE_CHARACTER_MASK) {
				SendPureCharacter(pData->macroData[i]);
			} else {
				SendKeyCode(pData->macroData[i]);
			}
		}
	}
	SendKeyCode(_keycode | (_flag & MASK_SHIFT ? CAPS_MASK : 0));
}

static bool SetModifierMask(const Uint16& vkCode) {
	// For caps lock case, toggling the flag isn't enough. We need to check the actual state, which should be done before each key press.
	// Example: the caps lock state can be changed without the key being pressed, or the key toggle is made with admin privilege, making the app not able to detect the change.
	if (GetKeyState(VK_CAPITAL) == 1) _flag |= MASK_CAPITAL;
	else _flag &= ~MASK_CAPITAL;

	if (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) _flag |= MASK_SHIFT;
	else if (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) _flag |= MASK_CONTROL;
	else if (vkCode == VK_LMENU || vkCode == VK_RMENU) _flag |= MASK_ALT;
	else if (vkCode == VK_LWIN || vkCode == VK_RWIN) _flag |= MASK_WIN;
	else if (vkCode == VK_NUMLOCK) _flag |= MASK_NUMLOCK;
	else if (vkCode == VK_SCROLL) _flag |= MASK_SCROLL;
	else { 
		_isFlagKey = false;
		return false; 
	}
	_isFlagKey = true;
	return true;
}

static bool UnsetModifierMask(const Uint16& vkCode) {
	if (vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) _flag &= ~MASK_SHIFT;
	else if (vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) _flag &= ~MASK_CONTROL;
	else if (vkCode == VK_LMENU || vkCode == VK_RMENU) _flag &= ~MASK_ALT;
	else if (vkCode == VK_LWIN || vkCode == VK_RWIN) _flag &= ~MASK_WIN;
	else if (vkCode == VK_NUMLOCK) _flag &= ~MASK_NUMLOCK;
	else if (vkCode == VK_SCROLL) _flag &= ~MASK_SCROLL;
	else { 
		_isFlagKey = false;
		return false; 
	}
	_isFlagKey = true;
	return true;
}

static LRESULT passThroughKey(int nCode, WPARAM wParam, LPARAM lParam);

static LRESULT keyboardHookProcessCore(int nCode, WPARAM wParam, LPARAM lParam) {
	keyboardData = (KBDLLHOOKSTRUCT *)lParam;
	//ignore my event
	if (keyboardData->dwExtraInfo != 0) {
		return passThroughKey(nCode, wParam, lParam);
	}
	
	//ignore if IME pad is open when typing Japanese/Chinese...
	HWND hWnd = GetForegroundWindow();
	HWND hIME = ImmGetDefaultIMEWnd(hWnd);
	LRESULT isImeON = SendMessage(hIME, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0);
	if (isImeON) {
		return passThroughKey(nCode, wParam, lParam);
	}
	
	//check modifier key
	if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
		//LOG(L"Key down: %d\n", keyboardData->vkCode);
		SetModifierMask((Uint16)keyboardData->vkCode);
	} else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
		//LOG(L"Key up: %d\n", keyboardData->vkCode);
		UnsetModifierMask((Uint16)keyboardData->vkCode);
	}
	if (!_isFlagKey && wParam != WM_KEYUP && wParam != WM_SYSKEYUP)
		_keycode = (Uint16)keyboardData->vkCode;

	//switch language shortcut; convert hotkey
	if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && !_isFlagKey && _keycode != 0) {
		if (GET_SWITCH_KEY(vSwitchKeyStatus) != _keycode && GET_SWITCH_KEY(convertToolHotKey) != _keycode) {
			_lastFlag = 0;
		} else {
			if (GET_SWITCH_KEY(vSwitchKeyStatus) == _keycode && checkHotKey(vSwitchKeyStatus, GET_SWITCH_KEY(vSwitchKeyStatus) != 0xFE)) {
				switchLanguage();
				_hasJustUsedHotKey = true;
				_keycode = 0;
				return -1;
			}
			if (GET_SWITCH_KEY(convertToolHotKey) == _keycode && checkHotKey(convertToolHotKey, GET_SWITCH_KEY(convertToolHotKey) != 0xFE)) {
				AppDelegate::getInstance()->onQuickConvert();
				_hasJustUsedHotKey = true;
				_keycode = 0;
				return -1;
			}
		}
		_hasJustUsedHotKey = _lastFlag != 0;
	} else if (_isFlagKey) {
		if (_lastFlag == 0 || _lastFlag < _flag)
			_lastFlag = _flag;
		else if (_lastFlag > _flag) {
			//check switch
			if (checkHotKey(vSwitchKeyStatus, GET_SWITCH_KEY(vSwitchKeyStatus) != 0xFE)) {
				switchLanguage();
				_hasJustUsedHotKey = true;
			}
			if (checkHotKey(convertToolHotKey, GET_SWITCH_KEY(convertToolHotKey) != 0xFE)) {
				AppDelegate::getInstance()->onQuickConvert();
				_hasJustUsedHotKey = true;
			}
			//check temporarily turn off spell checking
			if (vTempOffSpelling && !_hasJustUsedHotKey && _lastFlag & MASK_CONTROL) {
				vTempOffSpellChecking();
			}
			if (vTempOffOpenKey && !_hasJustUsedHotKey && _lastFlag & MASK_ALT) {
				vTempOffEngine();
			}
			_lastFlag = _flag;
			_hasJustUsedHotKey = false;
		}
		_keycode = 0;
		return passThroughKey(nCode, wParam, lParam);
	}

	//if is in english mode
	if (vLanguage == 0) {
		if (vUseMacro && vUseMacroInEnglishMode && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
			vEnglishMode(((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) ? vKeyEventState::KeyDown : vKeyEventState::MouseDown),
				_keycode,
				(_flag & MASK_SHIFT) || (_flag & MASK_CAPITAL),
				OTHER_CONTROL_KEY);

			if (pData->code == vReplaceMaro) { //handle macro in english mode
				handleMacro();
				return NULL;
			}
		}
		return passThroughKey(nCode, wParam, lParam);
	}

	//handle keyboard
	if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
		//send event signal to Engine
		vKeyHandleEvent(vKeyEvent::Keyboard,
						vKeyEventState::KeyDown,
						_keycode,
						(_flag & MASK_SHIFT && _flag & MASK_CAPITAL) ? 0 : (_flag & MASK_SHIFT ? 1 : (_flag & MASK_CAPITAL ? 2 : 0)),
						OTHER_CONTROL_KEY);
		if (pData->code == vDoNothing) { //do nothing
			if (IS_DOUBLE_CODE(vCodeTable)) { //VNI
				if (pData->extCode == 1) { //break key
					_syncKey.clear();
				} else if (pData->extCode == 2) { //delete key
					if (_syncKey.size() > 0) {
						if (_syncKey.back() > 1 && (vCodeTable == 2 || vCodeTable == 3)) {
							//send one more backspace
							SendInput(2, backspaceEvent, sizeof(INPUT));
						}
						_syncKey.pop_back();
					}
				} else if (pData->extCode == 3) { //normal key
					InsertKeyLength(1);
				}
			}
			return passThroughKey(nCode, wParam, lParam);
		} else if (pData->code == vWillProcess || pData->code == vRestore || pData->code == vRestoreAndStartNewSession) { //handle result signal
			//fix autocomplete
			if (vFixRecommendBrowser && pData->extCode != 4) {
				if (vFixChromiumBrowser && 
					std::find(_chromiumBrowser.begin(), _chromiumBrowser.end(), OpenKeyHelper::getLastAppExecuteName()) != _chromiumBrowser.end()) {
					SendCombineKey(KEY_LEFT_SHIFT, KEY_LEFT, 0, KEYEVENTF_EXTENDEDKEY);
					if (pData->backspaceCount == 1)
						pData->backspaceCount--;
				} else {
					SendEmptyCharacter();
					pData->backspaceCount++;
				}
			}
			
			//send backspace
			if (pData->backspaceCount > 0 && pData->backspaceCount < MAX_BUFF) {
				for (_i = 0; _i < pData->backspaceCount; _i++) {
					SendBackspace();
				}
			}

			//send new character
			if (!vSendKeyStepByStep) {
				SendNewCharString();
			} else {
				if (pData->newCharCount > 0 && pData->newCharCount <= MAX_BUFF) {
					for (int i = pData->newCharCount - 1; i >= 0; i--) {
						SendKeyCode(pData->charData[i]);
					}
				}
				if (pData->code == vRestore || pData->code == vRestoreAndStartNewSession) {
					SendKeyCode(_keycode | ((_flag & MASK_CAPITAL) || (_flag & MASK_SHIFT) ? CAPS_MASK : 0));
				}
				if (pData->code == vRestoreAndStartNewSession) {
					startNewSession();
				}
			}
		} else if (pData->code == vReplaceMaro) { //MACRO
			handleMacro();
		}
		return -1; //consume event
	}
	return passThroughKey(nCode, wParam, lParam);
}

static LRESULT passThroughKey(int nCode, WPARAM wParam, LPARAM lParam) {
	_passedThrough = true;
	return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

//---------------------------------------------------------------- pause in remote desktop apps
//Moonlight and UltraViewer send the keys to another computer, which has its own
//keyboard settings: NhaKey leaves every key alone while one of them is in front
static const wchar_t* _pauseApps[] = { L"moonlight", L"ultraviewer" };
bool vPausedForApp = false;
wstring vPausedAppName;
static HWND _pauseCheckedWindow = NULL;

static wstring ExeNameOf(HWND hWnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hWnd, &pid);
	wstring name;
	HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); //works for admin apps too
	if (proc) {
		WCHAR path[MAX_PATH];
		DWORD size = MAX_PATH;
		if (QueryFullProcessImageNameW(proc, 0, path, &size)) {
			const WCHAR* slash = wcsrchr(path, L'\\');
			name = slash ? slash + 1 : path;
		}
		CloseHandle(proc);
	}
	return name;
}

//the keys pressed outside NhaKey's sight: read them again
static void RefreshModifierFlags() {
	_flag &= ~(MASK_SHIFT | MASK_CONTROL | MASK_ALT | MASK_WIN | MASK_CAPITAL);
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) _flag |= MASK_SHIFT;
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000) _flag |= MASK_CONTROL;
	if (GetAsyncKeyState(VK_MENU) & 0x8000) _flag |= MASK_ALT;
	if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) _flag |= MASK_WIN;
	if (GetKeyState(VK_CAPITAL) & 1) _flag |= MASK_CAPITAL;
	_lastFlag = 0;
}

//true while NhaKey must not touch any key (cheap: only looks again when the front window changes)
static bool CheckPauseForApp() {
	HWND hWnd = GetForegroundWindow();
	if (hWnd == _pauseCheckedWindow)
		return vPausedForApp;
	_pauseCheckedWindow = hWnd;
	wstring exe = hWnd ? ExeNameOf(hWnd) : wstring();
	wstring lower = exe;
	for (size_t i = 0; i < lower.size(); i++) lower[i] = towlower(lower[i]);
	bool paused = false;
	for (const wchar_t* app : _pauseApps)
		if (lower.compare(0, wcslen(app), app) == 0)
			paused = true;
	if (paused != vPausedForApp) {
		vPausedForApp = paused;
		vPausedAppName = exe;
		if (vPausedAppName.size() > 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0)
			vPausedAppName.resize(vPausedAppName.size() - 4);
		//start clean on both sides of the pause
		startNewSession();
		_autoSwitch.reset();
		_autoSwitch.invalidate();
		_autoSwitch.breakPair();
		_screenCount = 0;
		_syncKey.clear();
		RefreshModifierFlags();
		SystemTrayHelper::updateData();
	}
	return vPausedForApp;
}

//---------------------------------------------------------------- keyboard layout
static int LayoutOf(HKL hkl) {
	UINT vk = MapVirtualKeyEx(0x1F, MAPVK_VSC_TO_VK, hkl); //the key right of A
	if (vk == 'S') return AUTO_LAYOUT_QWERTY;
	if (vk == 'O') return AUTO_LAYOUT_DVORAK;
	return AUTO_LAYOUT_UNKNOWN;
}

//the window that receives typing (a text box inside the foreground window)
static HWND GetFocusWindow() {
	HWND hWnd = GetForegroundWindow();
	GUITHREADINFO info = { sizeof(GUITHREADINFO) };
	if (GetGUIThreadInfo(GetWindowThreadProcessId(hWnd, NULL), &info) && info.hwndFocus)
		return info.hwndFocus;
	return hWnd;
}

//US key code of a character, the same code the app would see on that layout
static WORD VirtualKeyForChar(char32_t ch) {
	if (ch >= 'a' && ch <= 'z') return (WORD)(ch - 'a' + 'A');
	if (ch >= '0' && ch <= '9') return (WORD)ch;
	switch (ch) {
	case ';': return VK_OEM_1;
	case '=': return VK_OEM_PLUS;
	case ',': return VK_OEM_COMMA;
	case '-': return VK_OEM_MINUS;
	case '.': return VK_OEM_PERIOD;
	case '/': return VK_OEM_2;
	case '`': return VK_OEM_3;
	case '[': return VK_OEM_4;
	case '\\': return VK_OEM_5;
	case ']': return VK_OEM_6;
	case '\'': return VK_OEM_7;
	}
	return 0;
}

//NhaKey decides which layout the person is typing on (the wanted layout) and
//asks Windows to switch too. But apps switch late (the first letters after a
//switch came out on the old layout) or never (consoles, admin apps, some games).
//So while the app is not on the wanted layout yet, NhaKey types every letter
//itself: the switch is instant and works in every app.
static int _wantedLayout = AUTO_LAYOUT_UNKNOWN;
static int _lastAppLayout = AUTO_LAYOUT_UNKNOWN;
static DWORD _lastFocusThread = 0;
static bool _layoutTracked = false;
static struct { int layout; DWORD time; } _layoutRequests[2] = { { AUTO_LAYOUT_UNKNOWN, 0 }, { AUTO_LAYOUT_UNKNOWN, 0 } };

//did NhaKey ask for this layout a moment ago? (apps switch late, sometimes after the next request)
static bool RecentlyRequested(int layout) {
	for (auto& r : _layoutRequests)
		if (r.layout == layout && GetTickCount() - r.time < 2000)
			return true;
	return false;
}

static DWORD WINAPI LayoutSwitchThread(LPVOID param) {
	int layout = (int)(INT_PTR)param;
	HWND focus = GetFocusWindow();
	HWND top = GetForegroundWindow();
	HKL current = GetKeyboardLayout(GetWindowThreadProcessId(focus, NULL));
	HKL target = NULL;
	HKL list[64];
	int count = GetKeyboardLayoutList(64, list);
	for (int i = 0; i < count; i++) {
		if (LayoutOf(list[i]) != layout) continue;
		//prefer a layout of the same language as the current one
		if (!target || LOWORD((UINT_PTR)list[i]) == LOWORD((UINT_PTR)current))
			target = list[i];
	}
	if (!target) //not installed: load US / US-Dvorak for this session
		target = LoadKeyboardLayout(layout == AUTO_LAYOUT_DVORAK ? _T("00010409") : _T("00000409"), KLF_NOTELLSHELL);
	if (!target)
		return 0;
	//the app being typed in, and the system-wide input method so the language
	//bar on the taskbar shows the new layout (session only, not saved)
	PostMessage(focus, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
	if (top != focus)
		PostMessage(top, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target);
	SystemParametersInfo(SPI_SETDEFAULTINPUTLANG, 0, &target, SPIF_SENDCHANGE);
	return 0;
}

//ask Windows to switch, away from the keyboard hook: a slow hook makes Windows drop keys
static void RequestOsLayout(int layout) {
	_layoutRequests[1] = _layoutRequests[0];
	_layoutRequests[0] = { layout, GetTickCount() };
	HANDLE thread = CreateThread(NULL, 0, LayoutSwitchThread, (LPVOID)(INT_PTR)layout, 0, NULL);
	if (thread)
		CloseHandle(thread);
}

static void SwitchLayout(int layout) {
	_wantedLayout = layout;
	RequestOsLayout(layout);
}

//the layout keys must be read with right now; appLayoutOut gets the one the app really has
static int CurrentLayout(int* appLayoutOut = NULL) {
	DWORD thread = GetWindowThreadProcessId(GetFocusWindow(), NULL);
	int appLayout = LayoutOf(GetKeyboardLayout(thread));
	if (appLayoutOut)
		*appLayoutOut = appLayout;
	if (!vAutoSwitchLayout || !_layoutTracked) {
		_wantedLayout = appLayout;
	} else if (thread == _lastFocusThread) {
		//changed by hand (Win + Space, language bar): follow the person
		if (appLayout != _lastAppLayout && appLayout != _wantedLayout && !RecentlyRequested(appLayout))
			_wantedLayout = appLayout;
	} else if (appLayout != _wantedLayout && appLayout != AUTO_LAYOUT_UNKNOWN && _wantedLayout != AUTO_LAYOUT_UNKNOWN) {
		RequestOsLayout(_wantedLayout); //another window: bring the layout along
	}
	_layoutTracked = true;
	_lastFocusThread = thread;
	_lastAppLayout = appLayout;
	return _wantedLayout;
}

static int _previousWordScreen = -1; //characters of the word before the last space, -1 = unknown

static void TypeText(const std::u32string& text) {
	for (char32_t ch : text) {
		if (ch > 0xFFFF) continue;
		prepareUnicodeEvent(keyEvent[0], (Uint16)ch, true);
		prepareUnicodeEvent(keyEvent[1], (Uint16)ch, false);
		SendInput(2, keyEvent, sizeof(INPUT));
	}
}

//a word just ended: switch layout and/or retype the word. Returns true if the word was retyped.
static bool HandleAutoSwitchWordEnd(bool endsWithSpace) {
	autoSwitchSetModernOrthography(vUseModernOrthography != 0);
	AutoSwitchResult result = _autoSwitch.endWord(CurrentLayout(),
		vLanguage == 1 && vAutoSwitchLanguage && vCodeTable == 0,
		vAutoSwitchLayout != 0);
	if (!endsWithSpace)
		_autoSwitch.breakPair();
	if (result.switchLayout)
		SwitchLayout(result.layout);
	int wordScreen = _screenCount;
	if (!result.replace || _screenCount <= 0 || _screenCount > 64) {
		_previousWordScreen = endsWithSpace && wordScreen > 0 && wordScreen <= 64 ? wordScreen : -1;
		return false;
	}
	int deleteCount = _screenCount;
	bool fixPrevious = result.replacePrevious && _previousWordScreen > 0;
	if (fixPrevious) //the word before, and the space after it
		deleteCount += _previousWordScreen + 1;
	for (int i = 0; i < deleteCount; i++)
		SendInput(2, backspaceEvent, sizeof(INPUT));
	if (fixPrevious) {
		TypeText(result.previousText);
		TypeText(U" ");
	}
	TypeText(result.text);
	startNewSession();
	_previousWordScreen = endsWithSpace ? (int)result.text.size() : -1;
	return true;
}

//mid-word switch: the keys typed so far are replayed on the new layout so
//OpenKey's engine sees them again (Telex keeps working inside the word)
#define AUTO_REPLAY_TAG 0x4E4B5250
struct ReplayKey { UINT scanCode; bool shift; bool caps; };
static vector<ReplayKey> _replayQueue;

static void ReplayWordOnLayout(int layout) {
	vector<ReplayKey> keys;
	bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
	for (size_t i = 0; i < _autoSwitch.keyCount(); i++)
		keys.push_back({ _autoSwitch.keyScanCode(i), _autoSwitch.keyShift(i), caps });

	//delete what is on screen for this word, and the word before if it was typed on the new layout too
	int deleteCount = _screenCount > 0 && _screenCount <= 64 ? _screenCount : 0;
	std::u32string previousText;
	bool fixPrevious = _previousWordScreen > 0 && _autoSwitch.previousWordFix(layout, vLanguage == 1 && vCodeTable == 0, previousText);
	if (fixPrevious)
		deleteCount += _previousWordScreen + 1;
	for (int i = 0; i < deleteCount; i++)
		SendInput(2, backspaceEvent, sizeof(INPUT));
	if (fixPrevious) {
		TypeText(previousText);
		TypeText(U" ");
	}
	_previousWordScreen = -1;
	_screenCount = 0;
	startNewSession();
	_autoSwitch.reset();
	_autoSwitch.breakPair();
	_autoSwitch.setLanguageContext(0); //probably a different person typing now
	_autoSwitch.markEarlySwitch(CurrentLayout()); //goes back at the end of the word if this makes it no word

	SwitchLayout(layout);
	vector<INPUT> inputs;
	for (const ReplayKey& k : keys) {
		_replayQueue.push_back(k);
		INPUT in = {};
		in.type = INPUT_KEYBOARD;
		in.ki.wVk = VirtualKeyForChar(autoSwitchCharFor(k.scanCode, false, false, layout));
		in.ki.wScan = (WORD)k.scanCode;
		in.ki.dwExtraInfo = AUTO_REPLAY_TAG;
		inputs.push_back(in);
		in.ki.dwFlags = KEYEVENTF_KEYUP;
		inputs.push_back(in);
	}
	if (!inputs.empty())
		RawSendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
}

LRESULT CALLBACK keyboardHookProcess(int nCode, WPARAM wParam, LPARAM lParam) {
	KBDLLHOOKSTRUCT* data = (KBDLLHOOKSTRUCT*)lParam;
	if (nCode < 0)
		return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
	bool replayed = data->dwExtraInfo == AUTO_REPLAY_TAG;
	//Moonlight, UltraViewer...: every key goes to the other computer untouched
	if (!replayed && data->dwExtraInfo == 0 && CheckPauseForApp())
		return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);

	KBDLLHOOKSTRUCT copy = *data;
	if (replayed) { //let OpenKey handle it like a real key
		if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
			return 1;
		copy.dwExtraInfo = 0;
		data = &copy;
		lParam = (LPARAM)&copy;
	}
	if (data->dwExtraInfo != 0 || (!vAutoSwitchLayout && !vAutoSwitchLanguage))
		return keyboardHookProcessCore(nCode, wParam, lParam);
	if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN)
		return keyboardHookProcessCore(nCode, wParam, lParam);

	DWORD vk = data->vkCode;
	bool shortcut = (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
		(GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
	if (replayed) shortcut = false;

	switch (vk) {
	case VK_LSHIFT: case VK_RSHIFT: case VK_SHIFT: case VK_CAPITAL:
	case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL:
	case VK_LMENU: case VK_RMENU: case VK_MENU: case VK_LWIN: case VK_RWIN:
		return keyboardHookProcessCore(nCode, wParam, lParam);
	case VK_SPACE: case VK_RETURN: case VK_TAB: {
		bool hadWord = _autoSwitch.hasWord();
		if (!shortcut && hadWord && HandleAutoSwitchWordEnd(vk == VK_SPACE)) {
			_autoSwitch.reset();
			_screenCount = 0;
			return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam); //type the space itself
		}
		if (!hadWord) { //two spaces, enter on an empty line...
			_autoSwitch.breakPair();
			_previousWordScreen = -1;
		}
		_autoSwitch.reset();
		_screenCount = 0;
		LRESULT result = keyboardHookProcessCore(nCode, wParam, lParam);
		//OpenKey may rewrite the word at the space (spelling restore): keep its length right
		if (_previousWordScreen > 0)
			_previousWordScreen += _screenCount;
		if (_previousWordScreen <= 0 || _previousWordScreen > 64)
			_previousWordScreen = -1;
		_screenCount = 0;
		return result;
	}
	}

	if (!replayed && (shortcut || (data->flags & LLKHF_EXTENDED) || !autoSwitchIsTrackedKey(data->scanCode))) {
		_autoSwitch.invalidate(); //backspace, arrows, delete, shortcuts...
		_previousWordScreen = -1;
		return keyboardHookProcessCore(nCode, wParam, lParam);
	}

	//nobody typed for a while: maybe someone else sat down, switch sooner
	static DWORD lastKeyTime = 0;
	DWORD now = GetTickCount();
	if (!replayed && now - lastKeyTime > 10000)
		_autoSwitch.forgetConfidence();
	lastKeyTime = now;

	bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
	Uint32 savedFlag = _flag;
	if (replayed && !_replayQueue.empty()) {
		shift = _replayQueue.front().shift;
		caps = _replayQueue.front().caps;
		_replayQueue.erase(_replayQueue.begin());
		//OpenKey reads shift / caps from its flags
		_flag &= ~(MASK_SHIFT | MASK_CAPITAL);
		if (shift) _flag |= MASK_SHIFT;
		if (caps) _flag |= MASK_CAPITAL;
	}
	_autoSwitch.pushKey(data->scanCode, shift, caps);

	int appLayout = AUTO_LAYOUT_UNKNOWN;
	int layout = CurrentLayout(&appLayout);
	if (!replayed && vAutoSwitchLayout) {
		int target = _autoSwitch.checkEarly(layout);
		if (target != AUTO_LAYOUT_UNKNOWN) {
			ReplayWordOnLayout(target); //includes this key
			return 1;
		}
	}

	//the app is not on the wanted layout (yet): give OpenKey the key of the
	//wanted layout, and type the character ourselves if it passes through
	bool translate = layout != AUTO_LAYOUT_UNKNOWN &&
		(replayed || (appLayout != AUTO_LAYOUT_UNKNOWN && appLayout != layout));
	if (translate) {
		WORD translatedVk = VirtualKeyForChar(autoSwitchCharFor(data->scanCode, false, false, layout));
		if (translatedVk) {
			copy.vkCode = translatedVk;
			copy.dwExtraInfo = 0;
			data = &copy;
			lParam = (LPARAM)&copy;
		} else {
			translate = false;
		}
	}

	_passedThrough = false;
	LRESULT result = keyboardHookProcessCore(nCode, wParam, lParam);
	if (replayed)
		_flag = savedFlag;
	if (_passedThrough) {
		if (translate) {
			char32_t ch = autoSwitchCharFor(data->scanCode, shift, caps, layout);
			if (ch) {
				prepareUnicodeEvent(keyEvent[0], (Uint16)ch, true);
				prepareUnicodeEvent(keyEvent[1], (Uint16)ch, false);
				SendInput(2, keyEvent, sizeof(INPUT));
			}
			return 1;
		}
		if (replayed)
			return 1;
		_screenCount++; //the key itself reaches the app
	}
	return result;
}

LRESULT CALLBACK mouseHookProcess(int nCode, WPARAM wParam, LPARAM lParam) {
	mouseData = (MSLLHOOKSTRUCT *)lParam;
	switch (wParam) {
	case WM_LBUTTONDOWN:
	
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_XBUTTONDOWN:
	case WM_NCXBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP:
	case WM_NCXBUTTONUP:
		//send event signal to Engine
		vKeyHandleEvent(vKeyEvent::Mouse, vKeyEventState::MouseDown, 0);
		_autoSwitch.invalidate();
		if (IS_DOUBLE_CODE(vCodeTable)) { //VNI
			_syncKey.clear();
		}
		break;
	}
	return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

VOID CALLBACK winEventProcCallback(HWINEVENTHOOK hWinEventHook, DWORD dwEvent, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
	_autoSwitch.invalidate();
	CheckPauseForApp(); //pause / resume right away, so the tray icon shows it
	//smart switch key
	if (vUseSmartSwitchKey || vRememberCode) {
		string& exe = OpenKeyHelper::getFrontMostAppExecuteName();
		if (exe.compare("explorer.exe") == 0) //dont apply with windows explorer
			return;
		_languageTemp = getAppInputMethodStatus(exe, vLanguage | (vCodeTable << 1));
		vTempOffEngine(false);
		if (vUseSmartSwitchKey && (_languageTemp & 0x01) != vLanguage) {
			if (_languageTemp != -1) {
				vLanguage = _languageTemp;
				AppDelegate::getInstance()->onInputMethodChangedFromHotKey();
			} else {
				saveSmartSwitchKeyData();
			}
		}
		startNewSession();
		if (vRememberCode && (_languageTemp >> 1) != vCodeTable) { //for remember table code feature
			if (_languageTemp != -1) {
				AppDelegate::getInstance()->onTableCode(_languageTemp >> 1);
			} else {
				saveSmartSwitchKeyData();
			}
		}
		if (vSupportMetroApp && exe.compare("ApplicationFrameHost.exe") == 0) {//Metro App
			SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0L);
			SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0L);
		}
	}
}
