// Checks how TildePin rewrites keycodes, and that the mapping is a
// two-way swap so no key can disappear.
#define main tildepin_main
#include "../tildepin.c"
#undef main

static int fails;

static void expect(CGKeyCode in, CGKeyCode want, const char *what) {
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, in, true);
    CGEventRef out = on_event(NULL, kCGEventKeyDown, e, NULL);
    CGKeyCode got = (CGKeyCode)CGEventGetIntegerValueField(out, kCGKeyboardEventKeycode);
    if (got != want) {
        fails++;
        printf("FAIL %s: keycode %d became %d, wanted %d\n", what, in, got, want);
    }
    CFRelease(e);
}

static void expect_key_up(CGKeyCode in, CGKeyCode want) {
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, in, false);
    CGEventRef out = on_event(NULL, kCGEventKeyUp, e, NULL);
    CGKeyCode got = (CGKeyCode)CGEventGetIntegerValueField(out, kCGKeyboardEventKeycode);
    if (got != want) {
        fails++;
        printf("FAIL key up %d: became %d, wanted %d\n", in, got, want);
    }
    CFRelease(e);
}

int main(void) {
    // The ISO section key under Esc and the grave key left of Z trade places.
    expect(KEY_ISO_SECTION, KEY_GRAVE, "section key under Esc");
    expect(KEY_GRAVE, KEY_ISO_SECTION, "grave key left of Z");
    expect_key_up(KEY_ISO_SECTION, KEY_GRAVE);
    expect_key_up(KEY_GRAVE, KEY_ISO_SECTION);

    // Everything else is left alone.
    expect(0, 0, "A");         // kVK_ANSI_A
    expect(36, 36, "Return");  // kVK_Return
    expect(49, 49, "Space");   // kVK_Space
    expect(56, 56, "Shift");   // kVK_Shift

    // The table is a swap: every target is also a source, so pressing either
    // key twice in a row lands on the same keycode again.
    for (size_t i = 0; i < sizeof REMAP / sizeof REMAP[0]; i++) {
        if (remap_keycode(remap_keycode(REMAP[i].from)) != REMAP[i].from) {
            fails++;
            printf("FAIL keycode %d does not round-trip\n", REMAP[i].from);
        }
        if (REMAP[i].from == REMAP[i].to) {
            fails++;
            printf("FAIL keycode %d maps to itself\n", REMAP[i].from);
        }
    }

    // A disabled tap must not crash the handler: it is re-enabled only when
    // there is a tap to enable.
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, KEY_ISO_SECTION, true);
    if (on_event(NULL, kCGEventTapDisabledByTimeout, e, NULL) != e) {
        fails++;
        printf("FAIL a disabled-tap notice was not passed through\n");
    }
    CFRelease(e);

    // The live mapping is exactly the two keys; a stray entry would show up as
    // a keycode that changes but has no partner.
    if (remap_keycode(KEY_ISO_SECTION) != KEY_GRAVE || remap_keycode(KEY_GRAVE) != KEY_ISO_SECTION) {
        fails++;
        printf("FAIL the swap is not in the table\n");
    }

    printf(fails ? "%d failure(s)\n" : "all tests passed\n", fails);
    return fails != 0;
}
