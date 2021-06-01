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

#include "helper.h"

#include <stdint.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "DistrhoPluginInfo.h"
#include "ipc.h"
#include "log.h"

typedef struct {
    ipc_t*         ipc;
    Display*       display;
    GtkWindow*     window;
    WebKitWebView* webView;
} helper_context_t;

static void create_webview(helper_context_t *ctx);
static void navigate(const helper_context_t *ctx, const char *url);
static void reparent(const helper_context_t *ctx, uintptr_t parentId);
static void resize(const helper_context_t *ctx, const helper_size_t *size);
static void web_view_load_changed_cb(WebKitWebView *view, WebKitLoadEvent event, gpointer data);
static void window_destroy_cb(GtkWidget* widget, GtkWidget* window);
static gboolean ipc_read_cb(GIOChannel *source, GIOCondition condition, gpointer data);
static int ipc_write_simple(helper_context_t *ctx, helper_opcode_t opcode, const void *payload, int payload_sz);

int main(int argc, char* argv[])
{
    helper_context_t ctx;
    ipc_conf_t conf;
    GIOChannel* channel;

    if (argc < 3) {
        LOG_STDERR("Invalid argument count");
        return -1;
    }

    if ((sscanf(argv[1], "%d", &conf.fd_r) == 0) || (sscanf(argv[2], "%d", &conf.fd_w) == 0)) {
        LOG_STDERR("Invalid file descriptor");
        return -1;
    }

    ctx.ipc = ipc_init(&conf);

    gtk_init(0, NULL);
    
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())
            && ((ctx.display = XOpenDisplay(NULL)) == NULL)) {
        LOG_STDERR("Cannot open display");
        return -1;
    }

    create_webview(&ctx);

    channel = g_io_channel_unix_new(conf.fd_r);    
    g_io_add_watch(channel, G_IO_IN|G_IO_ERR|G_IO_HUP, ipc_read_cb, &ctx);

    gtk_main();

    g_io_channel_shutdown(channel, TRUE, NULL);
    ipc_destroy(ctx.ipc);

    return 0;
}

static void create_webview(helper_context_t *ctx)
{
    // Not sure what is the correct approach as both work
    ctx->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_decorated(ctx->window, FALSE);
    //ctx->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_POPUP));

    // TODO: gtk_widget_modify_bg() is deprecated
    int rgba = DISTRHO_UI_BACKGROUND_COLOR;
    GdkColor color = {0,
                      0x101 * (rgba >> 24),
                      0x101 * ((rgba & 0x00ff0000) >> 16),
                      0x101 * ((rgba & 0x0000ff00) >> 8)};
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_widget_modify_bg(GTK_WIDGET(ctx->window), GTK_STATE_NORMAL, &color);
    #pragma GCC diagnostic pop

    // Set up callback so that if the main window is closed, the program will exit
    g_signal_connect(ctx->window, "destroy", G_CALLBACK(window_destroy_cb), NULL);

    // Create a browser instance and put the browser area into the main window
    ctx->webView = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(ctx->window), GTK_WIDGET(ctx->webView));

    // Make sure that when the browser area becomes visible, it will get mouse and keyboard events
    gtk_widget_grab_focus(GTK_WIDGET(ctx->webView));

    g_signal_connect(ctx->webView, "load-changed", G_CALLBACK(web_view_load_changed_cb), NULL);
}

static void navigate(const helper_context_t *ctx, const char *url)
{
    // Window must be visible otherwise webkit_web_view_load_uri() fails
    gtk_widget_show(GTK_WIDGET(ctx->window));
    // Hide webview until it finishes loading to avoid some flicker
    gtk_widget_hide(GTK_WIDGET(ctx->webView));
    webkit_web_view_load_uri(ctx->webView, url);
}

static void reparent(const helper_context_t *ctx, uintptr_t parentId)
{
    GdkDisplay *gdkDisplay = gdk_display_get_default();
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        Window childId = gdk_x11_window_get_xid(gtk_widget_get_window(GTK_WIDGET(ctx->window)));
        XReparentWindow(ctx->display, childId, parentId, 0, 0);
        XFlush(ctx->display);
    } else if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default())) {
        // TODO: show a message in parent plugin window explaining that Wayland is not supported
        //       yet and because of that the plugin web user interface will be displayed in a
        //       separate window. Ideally include a button to focus such separate window.
        LOG_STDERR_COLOR("Running Wayland, plugin will be displayed in a separate window");
    }
}

static void resize(const helper_context_t *ctx, const helper_size_t *size)
{
    gtk_window_resize(ctx->window, size->width, size->height);
}

static void web_view_load_changed_cb(WebKitWebView *view, WebKitLoadEvent event, gpointer data)
{
    switch (event) {
        case WEBKIT_LOAD_FINISHED:
            // Load completed. All resources are done loading or there was an error during the load operation. 
            // Make sure the main window and all its contents are visible
            gtk_widget_show(GTK_WIDGET(view));
            break;
        default:
            break;
    }
}

static void window_destroy_cb(GtkWidget* widget, GtkWidget* window)
{
    gtk_main_quit();
}

static gboolean ipc_read_cb(GIOChannel *source, GIOCondition condition, gpointer data)
{
    helper_context_t *ctx = (helper_context_t *)data;
    tlv_t packet;

    if ((condition & G_IO_IN) == 0) {
        return TRUE;
    }

    if (ipc_read(ctx->ipc, &packet) == -1) {
        LOG_STDERR_ERRNO("Could not read from IPC channel");
        return TRUE;
    }

    switch (packet.t) {
        case OPC_NAVIGATE:
            navigate(ctx, (const char *)packet.v);
            break;
        case OPC_REPARENT:
            reparent(ctx, *((uintptr_t *)packet.v));
            break;
        case OPC_RESIZE:
            resize(ctx, (const helper_size_t *)packet.v);
            break;
        default:
            break;
    }

    return TRUE;
}

static int ipc_write_simple(helper_context_t *ctx, helper_opcode_t opcode, const void *payload, int payload_sz)
{
    int retval;
    tlv_t packet;

    packet.t = (short)opcode;
    packet.l = payload_sz;
    packet.v = payload;

    if ((retval = ipc_write(ctx->ipc, &packet)) == -1) {
        LOG_STDERR_ERRNO("Could not write to IPC channel");
    }

    return retval;
}
