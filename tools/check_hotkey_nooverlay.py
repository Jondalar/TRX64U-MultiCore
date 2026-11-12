#!/usr/bin/env python3
"""T181 D4: a hotkey action while the menu is closed draws nothing over the running core.
Checks coreapp/src/run.cc: the hotkey_act lambda and the hotkey branch of the main loop name no UI call
(ui_show, ui_toast_render, ui_menu_visible, ui_render_now, say, toast), and no hk_toast state is left.
--selftest puts the old ui_show back into hotkey_act and expects the check to fail."""
import os, re, sys

BAD = re.compile(r'\b(ui_show|ui_toast_render|ui_menu_visible|ui_render_now|ui_hide|say|toast_expire|hk_toast\w*)\b')

def lambda_body(src, head):
    i = src.index(head)
    j = src.index('{', i)
    d = 0
    for k in range(j, len(src)):
        d += (src[k] == '{') - (src[k] == '}')
        if d == 0:
            return src[j:k + 1]
    raise SystemExit('unbalanced')

def check(src):
    bad = []
    body = lambda_body(src, 'auto hotkey_act = [&](uint32_t f)')
    body = re.sub(r'//[^\n]*', '', body)
    bad += BAD.findall(body)
    i = src.index('uint32_t hf = hotkey_poll();')
    seg = re.sub(r'//[^\n]*', '', src[i:src.index('hw_menu_button_pressed()', i)])
    seg = seg.replace('open_menu();', '')
    bad += BAD.findall(seg)
    bad += re.findall(r'hk_toast\w*', src)
    return bad

if __name__ == '__main__':
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'coreapp', 'src', 'run.cc')
    src = open(path).read()
    if '--selftest' in sys.argv:
        mut = src.replace('printf("[core] hotkey: %s\\n", line);', 'printf("[core] hotkey: %s\\n", line); ui_show();', 1)
        assert mut != src
        if not check(mut):
            print('selftest: the old ui_show was not found'); sys.exit(1)
        print('selftest: ok (ui_show back in -> fails)'); sys.exit(0)
    b = check(src)
    if b:
        print('hotkey path draws over the running core:', sorted(set(b))); sys.exit(1)
    print('hotkey path draws nothing: ok')
