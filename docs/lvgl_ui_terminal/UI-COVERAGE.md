# UI coverage

The terminal variant mirrors all 30 routes from the primary LVGL prototype.
It does not maintain a second handwritten firmware inventory.

The authoritative mapping of Gideon's current menus, settings, task
collections and file actions is:

- [`../gideon-menu-tree.md`](../gideon-menu-tree.md)
- [`../lvgl_ui_project/UI-COVERAGE.md`](../lvgl_ui_project/UI-COVERAGE.md)
- [`../specs/T24-ui-wiring.md`](../specs/T24-ui-wiring.md)

The filenames in `screens/` intentionally match the primary project one for
one. Coverage checks should fail when either project gains or loses a screen
without the other being updated.

