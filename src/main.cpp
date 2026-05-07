#include "webview/webview.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main()
#endif
{
    try {
        webview::webview w(false, nullptr);

        w.set_title("Hardware Helper");
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
                    <h1>Hello World</h1>
                    <p>Cross-platform CI works.</p>
                </div>
            </body>
            </html>
        )");

        w.run();
    }
    catch (const webview::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}