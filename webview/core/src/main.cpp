#include "webview.h"

int main() {
    webview::webview w(true, nullptr);

    w.set_title("Hello");
    w.set_size(800, 600, WEBVIEW_HINT_NONE);

    w.set_html(R"(
        <html>
            <body>
                <h1>Hello from C++</h1>
                <button onclick="alert('Clicked!')">Click me</button>
            </body>
        </html>
    )");

    w.run();
    return 0;
}