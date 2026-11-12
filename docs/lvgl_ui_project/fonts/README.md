# Fonts

`LiberationSans-Regular.ttf` is a repository-relative symlink to the copy already
vendored with the `vendor/gideon` submodule. The prototype therefore adds no second
font binary. `globals.xml` compiles its ASCII range into five sizes with `<bin>`
and `as_file="false"`.

Keep the symlink relative so the project remains portable inside this repository.
If the upstream LVGL asset moves, update the link and reopen the Editor project.

LVGL Pro font syntax: https://lvgl.io/docs/pro/syntax/fonts
