// Prevents extra console window on Windows in release.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    rockchip_universal_imager_lib::run()
}
