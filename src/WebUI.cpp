/*
 * Hip-Hop / High Performance Hybrid Audio Plugins
 * Copyright (C) 2021 Luciano Iam <oss@lucianoiam.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "WebUI.hpp"
#include "Window.hpp"

#include "RuntimePath.hpp"

USE_NAMESPACE_DISTRHO

WebUI::WebUI()
    : UI(800, 600)  // TODO: avoid arbitrary size, plugin should be resizable
    , fParentWindowId(0)
{
    // Avoid black glitch on start
    uint rgba = getBackgroundColor();
    glClearColor((rgba >> 24) / 255.f,
                ((rgba & 0x00ff0000) >> 16) / 255.f,
                ((rgba & 0x0000ff00) >> 8) / 255.f,
                (rgba & 0x000000ff) / 255.f);
}

void WebUI::onDisplay()
{
    // onDisplay() can be called multiple times during lifetime of instance
    uintptr_t newParentWindowId = getParentWindow().getWindowId();
    
    if (fParentWindowId != newParentWindowId) {
        fParentWindowId = newParentWindowId;
        reparent(fParentWindowId);
    }
}

String WebUI::getContentUrl()
{
    return "file://" + rtpath::getResourcePath() + "/index.html";
}
