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

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "WebView.h"

void createWebView(uintptr_t windowPtr, const char *cUrl)
{
	// windowPtr is either a PuglCairoView* or PuglOpenGLViewDGL* depending UI_TYPE setting in Makefile
	// Both are NSView subclasses
	NSView *rootView = (NSView *)windowPtr;
	WKWebView *webView = [[WKWebView alloc] initWithFrame:rootView.frame];
	[rootView addSubview:webView];
	NSURL *url = [NSURL URLWithString:[NSString stringWithCString:cUrl encoding:NSUTF8StringEncoding]];
	[webView loadRequest:[NSURLRequest requestWithURL:url]];
}
