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

#include "ExternalGtkWebView.hpp"

#include <cstdio>
#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include "opcode.h"

/*
  Need to launch a separate process hosting the GTK web view because linking
  plugins to UI toolkit libraries like GTK or QT is known to be problematic.
*/

extern char **environ;

USE_NAMESPACE_DISTRHO

ExternalGtkWebView::~ExternalGtkWebView()
{
    terminate();
}

void ExternalGtkWebView::reparent(uintptr_t parentWindowId)
{
    if (!isRunning()) {
        spawn();
    }

    send(OPCODE_REPARENT, &parentWindowId, sizeof(parentWindowId));
}

int ExternalGtkWebView::spawn()
{
    if (isRunning()) {
        return -1;
    }

    if ((pipe(fPipeFd[0]) == -1) /*plugin->helper*/ || (pipe(fPipeFd[1]) == -1) /*p<-h*/) {
        perror("Could not create pipe");
        return -1;
    }

    fIpc = ipc_init(fPipeFd[1][0], fPipeFd[0][1]);

    fIpcThread.setIpc(fIpc);
    fIpcThread.startThread();

    char rfd[10];
    ::sprintf(rfd, "%d", fPipeFd[0][0]);
    char wfd[10];
    ::sprintf(wfd, "%d", fPipeFd[1][1]);
    const char *argv[] = {"d_dpf_webui_helper", rfd, wfd, NULL};
    const char* fixmeHardcodedPath = "/home/user/dpf-webui/bin/d_dpf_webui_helper";
    
    int status = posix_spawn(&fPid, fixmeHardcodedPath, NULL, NULL, (char* const*)argv, environ);

    if (status != 0) {
        perror("Could not spawn subprocess");
        return -1;
    }

    String url = getContentUrl();
    const char *cUrl = static_cast<const char *>(url);
    send(OPCODE_NAVIGATE, cUrl, ::strlen(cUrl) + 1);

    return 0;
}

void ExternalGtkWebView::terminate()
{
    fIpcThread.stopThread(-1);

    if (fPid != 0) {
        if (send(OPCODE_TERMINATE, NULL, 0) == -1) {
            kill(fPid, SIGKILL);
        }
    
        fPid = 0;
    }

    if (fIpc != 0) {
        ipc_destroy(fIpc);
        fIpc = 0;
    }

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            if ((fPipeFd[i][j] != 0) && (close(fPipeFd[i][j]) == -1)) {
                perror("Could not close pipe");
            }
            fPipeFd[i][j] = 0;
        }
    }
}

int ExternalGtkWebView::send(char opcode, const void *payload, int size)
{
    ipc_msg_t msg;
    msg.opcode = opcode;
    msg.payload_sz = size;
    msg.payload = payload;

    int retval;

    if ((retval = ipc_write(fIpc, &msg)) == -1) {
        perror("Could not write pipe");
    }

    return retval;
}

void HelperIpcReadThread::run()
{
    int fd = ipc_get_read_fd(fIpc);
    fd_set rfds;
    struct timeval tv;
    ipc_msg_t msg;

    while (true) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        int retval = select(fd + 1, &rfds, NULL, NULL, &tv);

        if (shouldThreadExit()) {
            break;
        }

        if (retval == 0) {
            continue;   // select() timeout
        }

        if (ipc_read(fIpc, &msg) == -1) {
            perror("Could not read pipe");
            break;
        }

        // FIXME
        fprintf(stderr, "Plugin Rx: %s\n", (const char*)msg.payload);
    }

}
