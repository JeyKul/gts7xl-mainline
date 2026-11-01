import St from 'gi://St';
import Clutter from 'gi://Clutter';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const DIM = 115; /* 0 = off, 255 = black; edit and log out to change */
const IDLE_MS = 60000; /* fully black after this long without input */

export default class DimExtension extends Extension {
    enable() {
        this._w = new St.Widget({
            style: 'background-color: black;',
            opacity: DIM,
            reactive: false,
        });
        this._w.add_constraint(new Clutter.BindConstraint({
            source: global.stage,
            coordinate: Clutter.BindCoordinate.ALL,
        }));
        Main.uiGroup.add_child(this._w);
        Main.uiGroup.set_child_above_sibling(this._w, null);

        this._monitor = global.backend.get_core_idle_monitor();
        this._idleId = this._monitor.add_idle_watch(IDLE_MS, () => this._black());
    }
    _black() {
        Main.uiGroup.set_child_above_sibling(this._w, null);
        this._w.opacity = 255;
        this._activeId = this._monitor.add_user_active_watch(() => {
            this._activeId = 0;
            this._w.opacity = DIM;
        });
    }
    disable() {
        if (this._activeId)
            this._monitor.remove_watch(this._activeId);
        this._monitor?.remove_watch(this._idleId);
        this._activeId = this._idleId = 0;
        this._monitor = null;
        this._w?.destroy();
        this._w = null;
    }
}
