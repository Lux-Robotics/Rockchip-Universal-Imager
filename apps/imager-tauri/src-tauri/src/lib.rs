mod commands;
mod logging;
mod paths;
mod platform;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    logging::init();

    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
            // Focus the existing window when a second launch is attempted.
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.set_focus();
            }
        }))
        .invoke_handler(tauri::generate_handler![
            commands::get_platform,
            commands::get_dependency_status,
            commands::get_log_contents,
            commands::open_log_directory,
            commands::ui_ready,
            commands::get_device_access_info,
            commands::select_image_file,
        ])
        .setup(|app| {
            tracing::info!(target: "app", "launched");
            let _ = app;
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
