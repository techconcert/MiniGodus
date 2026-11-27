"""The "Start a new world" button of the settings page (firmware/config_page.h and the gzip copy the clock really serves):
a plain button (never a submit button of the settings form), it asks first, it posts only to /config/world with an empty body and the page
then refreshes the status only (load(false)), never the form fields; the route is registered and the handler touches only the game."""
import gzip
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def read(p):
    with open(os.path.join(ROOT, p), encoding="utf-8") as f:
        return f.read()


def check(html, what):
    m = re.search(r'<button([^>]*)id="nw"[^>]*>', html)
    assert m and 'type="button"' in m.group(0), what + ": the new world button must be type=button"
    assert len(re.findall(r"<button>", html)) == 1, what + ": exactly one submit button (Save settings)"
    m = re.search(r"\$\('nw'\)\.onclick=\(\)=>\{if\(confirm\('([^']*)'\)\)post\('/config/world',new URLSearchParams\(\),'nw2'\)\};", html)
    assert m, what + ": the button asks first and posts an empty body to /config/world only"
    assert "setTimeout(()=>load(u=='/config'||u[8]=='r'),700)" in html, what + ": only /config and /config/reset refill the form"
    assert "/config/world"[8] == "w", "index 8 of the route is not r"
    assert html.count("/config/world") == 1


def main():
    src = read("firmware/config_page.h")
    check(src, "config_page.h")
    h = read("firmware/config_page_gz.h")
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", h))
    served = gzip.decompress(data).decode("utf-8")
    check(served, "config_page_gz.h (what the clock serves)")
    assert "id=\"nw\"" in served and "Start a new world" in served
    cpp = read("firmware/config.cpp")
    assert 's.on("/config/world", HTTP_POST, handleWorld);' in cpp
    body = re.search(r"static void handleWorld\(\) \{(.*?)\n\}", cpp, re.S).group(1)
    assert "gc_new_world(0)" in body and "store::saveSoon()" in body
    for bad in ("cfg_save", "cfg_load", "prefs::", "reset", "wxnet", "gqc_defaults", "gqs_defaults"):
        assert bad not in body, "handleWorld must touch only the game, found " + bad
    print("test_config_page: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
