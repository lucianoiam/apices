/*
 * Hip-Hap / High Performance Hybrid Audio Plugins
 * Copyright (C) 2021 Luciano Iam <oss@lucianoiam.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "EdgeWebWidget.hpp"

#include <cassert>
#include <codecvt>
#include <locale>
#include <sstream>
#include <winuser.h>

#include "dgl/Application.hpp"

#include "Platform.hpp"
#include "macro.h"
#include "extra/cJSON.h"

#include "KeyboardRouter.hpp"
#include "DistrhoPluginInfo.h"

#define WSTR_CONVERTER std::wstring_convert<std::codecvt_utf8<wchar_t>>()
#define TO_LPCWSTR(s)  WSTR_CONVERTER.from_bytes(s).c_str()
#define TO_LPCSTR(s)   WSTR_CONVERTER.to_bytes(s).c_str()

#define JS_POST_MESSAGE_SHIM "window.webviewHost.postMessage = (args) => window.chrome.webview.postMessage(args);"

USE_NAMESPACE_DISTRHO

EdgeWebWidget::EdgeWebWidget(Widget *parentWidget)
    : AbstractWebWidget(parentWidget)
    , fHelperHwnd(0)
    , fBackgroundColor(0)
    , fHandler(0)
    , fController(0)
    , fView(0)
{
    // Use a hidden orphan window for initializing Edge WebView2. Helps reducing
    // flicker and it is also required by the keyboard router for reading state.
    WCHAR className[256];
    swprintf(className, sizeof(className), L"EdgeWebWidget_%s_%d", XSTR(HIPHAP_PROJECT_ID_HASH), std::rand());
    ZeroMemory(&fHelperClass, sizeof(fHelperClass));
    fHelperClass.cbSize = sizeof(WNDCLASSEX);
    fHelperClass.cbClsExtra = sizeof(LONG_PTR);
    fHelperClass.lpszClassName = wcsdup(className);
    fHelperClass.lpfnWndProc = DefWindowProc;
    RegisterClassEx(&fHelperClass);
    fHelperHwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        fHelperClass.lpszClassName,
        L"EdgeWebWidget Helper",
        WS_POPUPWINDOW | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        0, 0, 0, 0
    );
    ShowWindow(fHelperHwnd, SW_SHOWNOACTIVATE);

    setKeyboardFocus(false);
    KeyboardRouter::getInstance().incRefCount();

    fHandler = new InternalWebView2EventHandler(this);

    // This request is queued until Edge WebView2 initializes itself
    String js = String(JS_POST_MESSAGE_SHIM);
    injectDefaultScripts(js);

    initWebView();
}

EdgeWebWidget::~EdgeWebWidget()
{
    fHandler->release();

    KeyboardRouter::getInstance().decRefCount();

    if (fController != 0) {
        ICoreWebView2Controller2_Close(fController);
        ICoreWebView2Controller2_Release(fController);
    }

    DestroyWindow(fHelperHwnd);
    UnregisterClass(fHelperClass.lpszClassName, 0);
    free((void*)fHelperClass.lpszClassName);
}

void EdgeWebWidget::onResize(const ResizeEvent& ev)
{
    (void)ev;
    if (fController == 0) {
        return; // discard
    }

    updateWebViewBounds();
}

void EdgeWebWidget::onPositionChanged(const PositionChangedEvent& ev)
{
    (void)ev;
    if (fController == 0) {
        return; // discard
    }

    updateWebViewBounds();
}

bool EdgeWebWidget::onKeyboard(const KeyboardEvent& ev)
{
    (void)ev;
    return false; // KeyboardRouter already takes care of this
}

void EdgeWebWidget::setBackgroundColor(uint32_t rgba)
{
    if (fController == 0) {
        fBackgroundColor = rgba;
        return; // queue
    }

    // Edge WebView2 currently only supports alpha=0 or alpha=1
    COREWEBVIEW2_COLOR color;
    color.A = static_cast<BYTE>(rgba & 0x000000ff);
    color.R = static_cast<BYTE>(rgba >> 24);
    color.G = static_cast<BYTE>((rgba & 0x00ff0000) >> 16);
    color.B = static_cast<BYTE>((rgba & 0x0000ff00) >> 8 );
    ICoreWebView2Controller2_put_DefaultBackgroundColor(
        reinterpret_cast<ICoreWebView2Controller2 *>(fController), color);
}

void EdgeWebWidget::navigate(String& url)
{
    if (fView == 0) {
        fUrl = url;
        return; // queue
    }

    ICoreWebView2_Navigate(fView, TO_LPCWSTR(url));
}

void EdgeWebWidget::runScript(String& source)
{
    // For the plugin specific use case fView==0 means a programming error.
    // There is no point in queuing these, just wait for the view to load its
    // contents before trying to run scripts. Otherwise use injectScript().
    assert(fView != 0);
    ICoreWebView2_ExecuteScript(fView, TO_LPCWSTR(source), 0);
}

void EdgeWebWidget::injectScript(String& source)
{
    if (fController == 0) {
        fInjectedScripts.push_back(source);
        return; // queue
    }

    ICoreWebView2_AddScriptToExecuteOnDocumentCreated(fView, TO_LPCWSTR(source), 0);
}

void EdgeWebWidget::setKeyboardFocus(bool focus)
{
    AbstractWebWidget::setKeyboardFocus(focus);
    SetClassLongPtr(fHelperHwnd, 0, (LONG_PTR)focus); // allow KeyboardRouter to read it
}

void EdgeWebWidget::updateWebViewBounds()
{
    RECT bounds;
    bounds.left = (LONG)getAbsoluteX();
    bounds.top = (LONG)getAbsoluteY();
    bounds.right = bounds.left + (LONG)getWidth();
    bounds.bottom = bounds.top + (LONG)getHeight();
    ICoreWebView2Controller2_put_Bounds(fController, bounds);
}

void EdgeWebWidget::initWebView()
{
    // Make sure COM is initialized - 0x800401F0 CO_E_NOTINITIALIZED
    CoInitializeEx(0, COINIT_APARTMENTTHREADED);

    HRESULT result = CreateCoreWebView2EnvironmentWithOptions(0,
                        TO_LPCWSTR(platform::getTemporaryPath()), 0, fHandler);
    if (FAILED(result)) {
        webViewLoaderErrorMessageBox(result);
        return;
    }

    if (getWindow().getApp().isStandalone()) {
        // handleWebView2ControllerCompleted() never gets called when running
        // standalone unless pumping thread message queue. Possibly related to
        // https://github.com/MicrosoftEdge/WebView2Feedback/issues/920

        MSG msg;
        
        while ((fController == 0) && GetMessage(&msg, 0, 0, 0)) {
            TranslateMessage(&msg); 
            DispatchMessage(&msg); 
        }

        // WINJACKBUG - handleWebView2NavigationCompleted() never gets called
        // when running standalone unless an "open window menu" event is
        // simulated. Otherwise the user needs to click the window border to
        // allow web content to load, providing DISTRHO_UI_USER_RESIZABLE=1.
        // Worth noting all DPF standalone examples on Windows appear off-screen
        // with the NC area hidden. Not sure it's a DPF or WebView2 issue though.

        HWND hWnd = reinterpret_cast<HWND>(getWindow().getNativeWindowHandle());
        PostMessage(hWnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
    }
}

HRESULT EdgeWebWidget::handleWebView2EnvironmentCompleted(HRESULT result,
                                                        ICoreWebView2Environment* environment)
{
    if (FAILED(result)) {
        webViewLoaderErrorMessageBox(result);
        return result;
    }

    ICoreWebView2Environment_CreateCoreWebView2Controller(environment, fHelperHwnd, fHandler);

    return S_OK;
}

HRESULT EdgeWebWidget::handleWebView2ControllerCompleted(HRESULT result,
                                                       ICoreWebView2Controller* controller)
{
    if (FAILED(result)) {
        webViewLoaderErrorMessageBox(result);
        return result;
    }

    fController = controller;

    ICoreWebView2Controller2_AddRef(fController);
    ICoreWebView2Controller2_get_CoreWebView2(fController, &fView);
    ICoreWebView2_add_NavigationCompleted(fView, fHandler, 0);
    ICoreWebView2_add_WebMessageReceived(fView, fHandler, 0);

    // Run pending requests

    setBackgroundColor(fBackgroundColor);
    updateWebViewBounds();

    for (std::vector<String>::iterator it = fInjectedScripts.begin(); it != fInjectedScripts.end(); ++it) {
        injectScript(*it);
    }

    navigate(fUrl);

    fBackgroundColor = 0;
    fInjectedScripts.clear();
    fUrl.clear();

    return S_OK;
}

HRESULT EdgeWebWidget::handleWebView2NavigationCompleted(ICoreWebView2 *sender,
                                                       ICoreWebView2NavigationCompletedEventArgs *eventArgs)
{
    (void)sender;
    (void)eventArgs;

    if (fController != 0) {
        // Reparent here instead of handleWebView2ControllerCompleted() to avoid
        // flicker as much as possible. At this point the web contents are ready.
        HWND hWnd = reinterpret_cast<HWND>(getWindow().getNativeWindowHandle());
        SetParent(fHelperHwnd, hWnd); // Allow EnumChildProc() to find the helper window
        ShowWindow(fHelperHwnd, SW_HIDE);

        ICoreWebView2Controller2_put_ParentWindow(fController, hWnd);

        handleLoadFinished();
    }
    
    return S_OK;
}

HRESULT EdgeWebWidget::handleWebView2WebMessageReceived(ICoreWebView2 *sender,
                                                      ICoreWebView2WebMessageReceivedEventArgs *eventArgs)
{
    // Edge WebView2 does not provide access to JSCore values; resort to parsing JSON
    (void)sender;

    LPWSTR jsonStr;
    ICoreWebView2WebMessageReceivedEventArgs_get_WebMessageAsJson(eventArgs, &jsonStr);
    cJSON* jArgs = cJSON_Parse(TO_LPCSTR(jsonStr));
    CoTaskMemFree(jsonStr);

    ScriptValueVector args;
    
    if (cJSON_IsArray(jArgs)) {
        int numArgs = cJSON_GetArraySize(jArgs);

        if (numArgs > 0) {
            for (int i = 0; i < numArgs; i++) {
                cJSON* jArg = cJSON_GetArrayItem(jArgs, i);

                if (cJSON_IsFalse(jArg)) {
                    args.push_back(ScriptValue(false));
                } else if (cJSON_IsTrue(jArg)) {
                    args.push_back(ScriptValue(true));
                } else if (cJSON_IsNumber(jArg)) {
                    args.push_back(ScriptValue(cJSON_GetNumberValue(jArg)));
                } else if (cJSON_IsString(jArg)) {
                    args.push_back(ScriptValue(String(cJSON_GetStringValue(jArg))));
                } else {
                    args.push_back(ScriptValue()); // null
                }
            }
        }
    }

    cJSON_free(jArgs);

    handleScriptMessage(args);
    
    return S_OK;
}

void EdgeWebWidget::webViewLoaderErrorMessageBox(HRESULT result)
{
    // TODO: Add clickable link to installer. It would be also better to display
    //       a message and button in the native window using DGL widgets.
    std::wstringstream wss;
    wss << "Make sure you have installed the Microsoft Edge WebView2 Runtime. "
        << "Error 0x" << std::hex << result;
    std::wstring ws = wss.str();
    
    HIPHAP_LOG_STDERR_COLOR(TO_LPCSTR(ws));

    MessageBox(0, ws.c_str(), TEXT(DISTRHO_PLUGIN_NAME), MB_OK | MB_ICONSTOP);
}
