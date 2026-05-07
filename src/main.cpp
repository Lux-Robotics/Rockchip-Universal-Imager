#include "webview.h"

int main() {
    webview::webview w(true, nullptr);

    w.set_title("Lux WebView");
    w.set_size(800, 600, WEBVIEW_HINT_NONE);

    w.set_html(R"(
        <!doctype html>
        <html>
        <body style="
            background:#111;
            color:white;
            font-family:sans-serif;
            display:flex;
            justify-content:center;
            align-items:center;
            height:100vh;
            margin:0;
        ">
            <div>
                <h1>Hello from WebView!</h1>
                <p>Cross-platform CI pipeline works.</p>
            </div>
        </body>
        </html>
    )");

    w.run();

    return 0;
}