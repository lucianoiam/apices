/*
 * dpf-webui
 * Copyright (C) 2021 Luciano Iam <lucianoiam@protonmail.com>
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

#include "EdgeWebViewUI.hpp"

#include <codecvt>
#include <locale>
#include <sstream>
#include <shellscalingapi.h>
#include <shtypes.h>

#include "DistrhoPluginInfo.h"
#include "RuntimePath.hpp"
#include "WinApiStub.hpp"
#include "log.h"

USE_NAMESPACE_DISTRHO

UI* DISTRHO::createUI()
{
    return new EdgeWebViewUI(EdgeWebViewUI::getDpiAwareScaleFactor());
}

EdgeWebViewUI::EdgeWebViewUI(float scale)
    : WebUI(scale)
    , fController(0)
{
	// Initializing a WebView2 requires a HWND but parent window not available yet
}

EdgeWebViewUI::~EdgeWebViewUI()
{
    cleanupWebView();
}

void EdgeWebViewUI::parameterChanged(uint32_t index, float value)
{
    // unused
    (void)index;
    (void)value;
}

void EdgeWebViewUI::reparent(uintptr_t windowId)
{
	(void)windowId;
	
    cleanupWebView();

    // See handleWebViewControllerCompleted()
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring temp = converter.from_bytes(rtpath::getTemporaryPath());
    HRESULT result = ::CreateCoreWebView2EnvironmentWithOptions(0, temp.c_str(), 0, this);

    if (FAILED(result)) {
        errorMessageBox(L"Could not create WebView2 environment options", result);
    }
}

HRESULT EdgeWebViewUI::handleWebViewEnvironmentCompleted(HRESULT result,
                                                         ICoreWebView2Environment* environment)
{
    if (FAILED(result)) {
        errorMessageBox(L"Could not create WebView2 environment", result);
        return result;
    }

    ICoreWebView2Environment_CreateCoreWebView2Controller(environment, (HWND)getParentWindowId(), this);

    return S_OK;
}

HRESULT EdgeWebViewUI::handleWebViewControllerCompleted(HRESULT result,
                                                        ICoreWebView2Controller* controller)
{
    if (FAILED(result)) {
        errorMessageBox(L"Could not create WebView2 controller", result);
        return result;
    }

    fController = controller;
    ICoreWebView2Controller2_AddRef(fController);
    ICoreWebView2Controller2_put_IsVisible(fController, false);

    // Not sure about the legality of the cast below
    uint rgba = getBackgroundColor();
    COREWEBVIEW2_COLOR color;
    color.R = rgba >> 24;
    color.G = (rgba & 0x00ff0000) >> 16;
    color.B = (rgba & 0x0000ff00) >> 8;
    color.A = 0xff; // alpha does not seem to work
    ICoreWebView2Controller2_put_DefaultBackgroundColor(
        reinterpret_cast<ICoreWebView2Controller2 *>(fController), color);

	ICoreWebView2* webView;
    ICoreWebView2Controller2_get_CoreWebView2(fController, &webView);
    ICoreWebView2_AddRef(webView);
    ICoreWebView2_add_NavigationCompleted(webView, this, /* token */0);

    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring url = converter.from_bytes(getContentUrl());
    ICoreWebView2_Navigate(webView, url.c_str());

    resize();

    return S_OK;
}

HRESULT EdgeWebViewUI::handleWebViewNavigationCompleted(ICoreWebView2 *sender,
                                                        ICoreWebView2NavigationCompletedEventArgs *eventArgs)
{
    (void)sender;
    (void)eventArgs;
    ICoreWebView2Controller2_put_IsVisible(fController, true);
    return S_OK;
}

void EdgeWebViewUI::cleanupWebView()
{
    if (fController != 0) {
        ICoreWebView2Controller2_Close(fController);
    }

    fController = 0;
}

void EdgeWebViewUI::resize()
{
    if (fController == 0) {
        return;
    }

    RECT bounds;
    HRESULT result = ::GetClientRect((HWND)getParentWindowId(), &bounds);

    if (FAILED(result)) {
        LOG_STDERR_INT("Could not determine parent window size", result);
        return;
    }

    ICoreWebView2Controller2_put_Bounds(fController, bounds);
}

float EdgeWebViewUI::getDpiAwareScaleFactor()
{
    float k = 1.f;
    PROCESS_DPI_AWARENESS dpiAware;

    if (SUCCEEDED(stub::GetProcessDpiAwareness(0, &dpiAware))) {
        if (dpiAware != PROCESS_DPI_UNAWARE) {
            HMONITOR hMon = ::MonitorFromWindow(::GetConsoleWindow(), MONITOR_DEFAULTTOPRIMARY);
            DEVICE_SCALE_FACTOR scaleFactor;

            if (SUCCEEDED(stub::GetScaleFactorForMonitor(hMon, &scaleFactor))) {
                if (scaleFactor != DEVICE_SCALE_FACTOR_INVALID) {
                    k = static_cast<float>(scaleFactor) / 100.f;
                }
            }
        } else {
            // Process is not DPI-aware, do not scale
        }
    }

    return k;
}

void EdgeWebViewUI::errorMessageBox(std::wstring message, HRESULT result)
{
    std::wstringstream ss;
    ss << message << ", HRESULT 0x" << std::hex << result;
    ::MessageBox(0, ss.str().c_str(), TEXT(DISTRHO_PLUGIN_NAME), MB_OK | MB_ICONSTOP);
}
