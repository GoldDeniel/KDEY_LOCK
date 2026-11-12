import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const ICON_OFF = 'input-keyboard-symbolic';
const ICON_ON = 'changes-prevent-symbolic';

export default class KeyboardCleanExtension extends Extension {
    enable() {
        this._bin = GLib.find_program_in_path('keyclean') || '/usr/local/bin/keyclean';
        this._active = false;
        this._timer = 0;
        this._statusTimer = 0;

        this._indicator = new PanelMenu.Button(0.0, this.metadata.name, true);
        this._icon = new St.Icon({
            icon_name: ICON_OFF,
            style_class: 'system-status-icon',
        });
        this._indicator.add_child(this._icon);
        this._indicator.connect('button-press-event', () => {
            this._toggle();
            return Clutter.EVENT_STOP;
        });
        this._setActive(false);
        Main.panel.addToStatusArea(this.metadata.uuid, this._indicator);
        this._statusTimer = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 5, () => {
            this._sync();
            return GLib.SOURCE_CONTINUE;
        });
        this._sync();
    }

    disable() {
        if (this._timer) {
            GLib.source_remove(this._timer);
            this._timer = 0;
        }
        if (this._statusTimer) {
            GLib.source_remove(this._statusTimer);
            this._statusTimer = 0;
        }
        this._indicator?.destroy();
        this._indicator = null;
        this._icon = null;
    }

    _run(command) {
        try {
            return Gio.Subprocess.new(
                [this._bin, command],
                Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE);
        } catch (e) {
            logError(e, 'keyclean');
            return null;
        }
    }

    _sync() {
        const proc = this._run('status');
        if (!proc)
            return;

        proc.communicate_utf8_async(null, null, (p, res) => {
            try {
                const [, out] = p.communicate_utf8_finish(res);
                this._setActive(out.trim() === 'on');
            } catch (e) {
                logError(e, 'keyclean');
            }
        });
    }

    _toggle() {
        this._run('toggle');
        this._setActive(!this._active);

        if (this._timer)
            GLib.source_remove(this._timer);
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 400, () => {
            this._timer = 0;
            this._sync();
            return GLib.SOURCE_REMOVE;
        });
    }

    _setActive(active) {
        this._active = active;
        if (!this._indicator)
            return;
        this._icon.icon_name = active ? ICON_ON : ICON_OFF;
        this._indicator.accessible_name = active
            ? 'Keyboard ignored - click to restore'
            : 'Keyboard active - click to ignore keys';
    }
}
