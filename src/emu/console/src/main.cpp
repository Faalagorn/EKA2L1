/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <common/cvt.h>
#include <common/log.h>
#include <core/core.h>
#include <core/loader/rom.h>

#include <debugger/debugger.h>
#include <debugger/renderer/renderer.h>
#include <core/drivers/emu_window.h>

#include <imgui.h>
#include <yaml-cpp/yaml.h>

using namespace eka2l1;

eka2l1::system symsys;
eka2l1::arm::jitter_arm_type jit_type = decltype(jit_type)::unicorn;
epocver ever = epocver::epoc9;

std::string rom_path = "SYM.ROM";
std::string mount_c = "drives/c/";
std::string mount_e = "drives/e/";
std::string mount_z = "drives/z/";
std::string sis_install_path = "-1";

std::string rpkg_path;
std::uint16_t gdb_port = 24689;
std::uint8_t adrive;

bool enable_gdbstub = false;

YAML::Node config;

int drive_mount;
int app_idx = -1;

bool help_printed = false;
bool list_app = false;
bool install_rpkg = false;

std::atomic<bool> quit(false);
std::mutex ui_debugger_mutex;

ImGuiContext *ui_debugger_context;
std::shared_ptr<eka2l1::driver::emu_window> debugger_window;

void print_help() {
    std::cout << "Usage: Drag and drop Symbian file here, ignore missing dependencies" << std::endl;
    std::cout << "Options: " << std::endl;
    std::cout << "\t -rom: Specified where the ROM is located. If none is specified, the emu will look for a file named SYM.ROM." << std::endl;
    std::cout << "\t -ver: Specified Symbian version to emulate (either 6 or 9)." << std::endl;
    std::cout << "\t -app: Specified the app to run. Next to this option is the index number." << std::endl;
    std::cout << "\t -listapp: List all of the apps." << std::endl;
    std::cout << "\t -install: Install a SIS/SISX package" << std::endl;
    std::cout << "\t -irpkg ver path: Install RPKG." << std::endl;
    std::cout << "\t\t ver:  Epoc version. Available version are: v94, v93, belle, v60" << std::endl;
    std::cout << "\t\t path: Path to RPKG file." << std::endl;
    std::cout << "\t -h/-help: Print help" << std::endl;
}

void fetch_rpkg(const char *ver, const char *path) {
    install_rpkg = true;

    rpkg_path = path;
    std::string ver_str = ver;

    if (ver_str == "v93") {
        ever = epocver::epoc93;
    } else if (ver_str == "v94") {
        ever = epocver::epoc9;
    } else if (ver_str == "belle") {
        ever = epocver::epoc10;
    } else if (ver_str == "v60") {
        ever = epocver::epoc6;
    }
}

void parse_args(int argc, char **argv) {
    if (argc <= 1) {
        print_help();
        quit = true;
        return;
    }

    for (int i = 1; i <= argc - 1; i++) {
        if (strncmp(argv[i], "-rom", 4) == 0) {
            rom_path = argv[++i];
            config["rom_path"] = rom_path;
        } else if (((strncmp(argv[i], "-h", 2)) == 0 || (strncmp(argv[i], "-help", 5) == 0))
            && (!help_printed)) {
            print_help();
            help_printed = true;
            quit = true;
        } else if ((strncmp(argv[i], "-ver", 4) == 0 || (strncmp(argv[i], "-v", 2) == 0))) {
            int ver = std::atoi(argv[++i]);

            if (ver == 6) {
                ever = epocver::epoc6;
                config["epoc_ver"] = (int)ever;
            } else {
                ever = epocver::epoc9;
                config["epoc_ver"] = (int)ever;
            }
        } else if (strncmp(argv[i], "-app", 4) == 0) {
            try {
                app_idx = std::atoi(argv[++i]);
            } catch (...) {
                std::cout << "Invalid request." << std::endl;

                quit = true;
                break;
            }
        } else if (strncmp(argv[i], "-listapp", 8) == 0) {
            list_app = true;
        } else if (strncmp(argv[i], "-install", 8) == 0) {
            try {
                adrive = std::atoi(argv[++i]);
            } catch (...) {
                std::cout << "Invalid request." << std::endl;

                quit = true;
                break;
            }

            sis_install_path = argv[++i];
        } else if (strncmp(argv[i], "-mount", 6) == 0) {
            drive_mount = std::atoi(argv[++i]);

            if (drive_mount == 0) {
                mount_c = argv[++i];
            } else {
                mount_e = argv[++i];
            }
        } else if (strncmp(argv[i], "-irpkg", 6) == 0) {
            i += 2;

            fetch_rpkg(argv[i - 1], argv[i]);
        } else {
            std::cout << "Invalid request." << std::endl;

            quit = true;
            break;
        }
    }
}

void read_config() {
    try {
        config = YAML::LoadFile("config.yml");

        rom_path = config["rom_path"].as<std::string>();
        ever = (epocver)(config["epoc_ver"].as<int>());

        const std::string jit_type_raw = config["jitter"].as<std::string>();

        if (jit_type_raw == "dynarmic") {
            jit_type = decltype(jit_type)::dynarmic;
        }

        enable_gdbstub = config["enable_gdbstub"].as<bool>();
        gdb_port = config["gdb_port"].as<int>();
    } catch (...) {
        return;
    }
}

void do_args() {
    auto infos = symsys.app_infos();

    if (list_app) {
        for (auto &info : infos) {
            std::cout << "[0x" << common::to_string(info.id, std::hex) << "]: " 
                      << common::ucs2_to_utf8(info.name) << " (drive: " << ((info.drive == 0) ? 'C' : 'E')
                      << " , executable name: " << common::ucs2_to_utf8(info.executable_name) << ")" << std::endl;
        }

        quit = true;
        return;
    }

    if (app_idx > -1) {
        if (app_idx >= infos.size()) {
            LOG_ERROR("Invalid app index.");
            quit = true;
            return;
        }

        symsys.load(infos[app_idx].id);
        return;
    }

    if (sis_install_path != "-1") {
        auto res = symsys.install_package(common::utf8_to_ucs2(sis_install_path), adrive);

        if (res) {
            std::cout << "Install successfully!" << std::endl;
        } else {
            std::cout << "Install failed" << std::endl;
        }

        quit = true;
    }

    if (install_rpkg) {
        symsys.set_symbian_version_use(ever);
        bool res = symsys.install_rpkg(rpkg_path);

        if (!res) {
            std::cout << "RPKG install failed." << std::endl;
        } else {
            std::cout << "RPKG install successfully." << std::endl;
        }

        quit = true;
    }
}

void init() {
    symsys.set_symbian_version_use(ever);
    symsys.set_jit_type(jit_type);

    symsys.init();
    symsys.mount(drive_c, drive_media::physical, mount_c, io_attrib::internal);
    symsys.mount(drive_e, drive_media::physical, mount_e, io_attrib::removeable);
    symsys.mount(drive_z, drive_media::rom,
        mount_z, io_attrib::internal | io_attrib::write_protected);

    if (enable_gdbstub) {
        symsys.get_gdb_stub()->set_server_port(gdb_port);
        symsys.get_gdb_stub()->init(&symsys);
        symsys.get_gdb_stub()->toggle_server(true);
    }

    bool res = symsys.load_rom(rom_path);
}

void shutdown() {
    symsys.shutdown();
}

void save_config() {
    config["rom_path"] = rom_path;
    config["epoc_ver"] = (int)ever;
    config["c_mount"] = mount_c;
    config["e_mount"] = mount_e;
    config["enable_gdbstub"] = enable_gdbstub;

    std::ofstream config_file("config.yml");
    config_file << config;
}

void do_quit() {
    save_config();
    symsys.shutdown();
}

void on_ui_window_mouse_evt(eka2l1::point mouse_pos, int button, int action) {
    ImGuiIO &io = ImGui::GetIO();
    io.MousePos = ImVec2(static_cast<float>(mouse_pos.x), 
        static_cast<float>(mouse_pos.y));

    if (action <= 1 || debugger_window->get_mouse_button_hold(button)) {
        io.MouseDown[button] = true;
    }
}

void on_ui_window_mouse_scrolling(eka2l1::vec2 v) {
    ImGuiIO &io = ImGui::GetIO();
    io.MouseWheel += static_cast<float>(v.y);
}

int ui_debugger_thread() {
    debugger_window = eka2l1::driver::new_emu_window(eka2l1::driver::window_type::glfw);
    
    debugger_window->raw_mouse_event = on_ui_window_mouse_evt;
    debugger_window->mouse_wheeling = on_ui_window_mouse_scrolling;

    debugger_window->init("Debugging Window", eka2l1::vec2(500, 500));
    debugger_window->make_current();

    /* Consider main thread not touching this, no need for mutex */
    ui_debugger_context = ImGui::CreateContext();

    auto debugger = std::make_shared<eka2l1::debugger>(&symsys);
    auto debugger_renderer = 
        eka2l1::new_debugger_renderer(eka2l1::debugger_renderer_type::opengl);

    debugger_renderer->init(debugger);

    while (!quit) {
        vec2 nws = debugger_window->window_size();
        vec2 nwsb = debugger_window->window_fb_size();

        debugger_window->poll_events();
        debugger_renderer->draw(nws.x, nws.y, nwsb.x, nwsb.y);
        debugger_window->swap_buffer();

        ImGuiIO &io = ImGui::GetIO();
        
        io.MouseWheel = 0;
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
    }

    ImGui::DestroyContext();
    debugger_renderer->deinit();
    debugger_window->done_current();
    debugger_window->shutdown();

    return 0;
}

#define FOREVER for (;;)

int main(int argc, char **argv) {
    std::cout << "-------------- EKA2L1: Experimental Symbian Emulator -----------------" << std::endl;

    read_config();
    parse_args(argc, argv);

    if (quit) {
        do_quit();
        return 0;
    }

    eka2l1::driver::init_window_library(eka2l1::driver::window_type::glfw);

    try {
        init();
        do_args();

        if (quit) {
            do_quit();
            return 0;
        }
    } catch (...) {
        std::cout << "Internal error happens in the compiler" << std::endl;

        do_quit();
    }

    std::thread debug_window_thread(ui_debugger_thread);

    try {
        while (!symsys.should_exit()) {
            symsys.loop();
        }
    } catch (...) {
        std::cout << "Internal error happens in the compiler" << std::endl;
    }

    debug_window_thread.join();
    do_quit();

    eka2l1::driver::destroy_window_library(eka2l1::driver::window_type::glfw);

    return 0;
}