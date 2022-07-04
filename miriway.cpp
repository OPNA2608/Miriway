/*
 * Copyright © 2022 Octopull Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alan Griffiths <alan@octopull.co.uk>
 */

#include "miriway_policy.h"
#include "miriway_commands.h"

#include <miral/append_event_filter.h>
#include <miral/command_line_option.h>
#include <miral/display_configuration_option.h>
#include <miral/external_client.h>
#include <miral/internal_client.h>
#include <miral/keymap.h>
#include <miral/runner.h>
#include <miral/set_window_management_policy.h>
#include <miral/version.h>
#include <miral/wayland_extensions.h>
#include <miral/x11_support.h>
#include <mir/fatal.h>
#include <mir/log.h>

#include <boost/filesystem.hpp>
#include <linux/input.h>
#include <csignal>

using namespace miral;
using namespace miriway;

int main(int argc, char const* argv[])
{
    MirRunner runner{argc, argv};

    // Protocols that are "experimental" in Mir but we want to allow
    auto const experimental_protocols = {"zwp_pointer_constraints_v1", "zwp_relative_pointer_manager_v1"};

    WaylandExtensions extensions;
    auto const supported_protocols = WaylandExtensions::supported();

    for (auto const& protocol : experimental_protocols)
    {
        if (supported_protocols.find(protocol) != end(supported_protocols))
        {
            extensions.enable(protocol);
        }
        else
        {
            mir::log_debug("This version of Mir doesn't support the Wayland extension %s", protocol);
        }
    }

    std::set<pid_t> shell_component_pids;
    ExternalClientLauncher external_client_launcher;

    auto run_apps = [&](std::string const& apps)
        {
        for (auto i = begin(apps); i != end(apps); )
        {
            auto const j = find(i, end(apps), ':');
            shell_component_pids.insert(external_client_launcher.launch({std::string{i, j}}));
            if ((i = j) != end(apps)) ++i;
        }
        };

    // Protocols we're reserving for shell components
    for (auto const& protocol : {
        WaylandExtensions::zwlr_layer_shell_v1,
        WaylandExtensions::zxdg_output_manager_v1,
        WaylandExtensions::zwlr_foreign_toplevel_manager_v1,
        WaylandExtensions::zwp_virtual_keyboard_manager_v1,
        WaylandExtensions::zwlr_screencopy_manager_v1,
        WaylandExtensions::zwp_input_method_manager_v2})
    {
        extensions.conditionally_enable(protocol, [&](WaylandExtensions::EnableInfo const& info)
            {
                return shell_component_pids.find(pid_of(info.app())) != end(shell_component_pids) ||
                    info.user_preference().value_or(false);
            });
    }

    auto const terminal_cmd = std::string{argv[0]} + "-terminal";
    auto const background_cmd = std::string{argv[0]} + "-background";
    auto const panel_cmd = std::string{argv[0]} + "-panel";
    ShellCommands commands{runner, external_client_launcher, terminal_cmd};

    int no_of_workspaces = 1;
    auto const update_workspaces = [&](int option)
        {
            // clamp no_of_workspaces to [1..32]
            no_of_workspaces = std::min(std::max(option, 1), 32);
        };

    return runner.run_with(
        {
            X11Support{},
            extensions,
            display_configuration_options,
            pre_init(CommandLineOption{update_workspaces,
                              "no-of-workspaces", "Number of workspaces [1..32]", no_of_workspaces}),
            external_client_launcher,
            CommandLineOption{run_apps, "shell-components", "Colon separated shell components to launch on startup",
                              (background_cmd + ":" + panel_cmd).c_str()},
//            CommandLineOption{app_launcher, "shell-app-launcher", "External app launcher command"},
            Keymap{},
            AppendEventFilter{[&](MirEvent const* e) { return commands.input_event(e); }},
            set_window_management_policy<WindowManagerPolicy>(commands, no_of_workspaces)
        });
}