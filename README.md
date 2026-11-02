<img src="assets/AppIcon.iconset/icon_256x256@2x.png" width="128" alt="TildePin icon">

# TildePin

Puts the `/~ key under Esc on an ISO keyboard. The extra key left of Z takes
over the §/± it used to type, so ` and ~ sit where an ANSI keyboard has them,
in every input source.

About 2 MB of memory, 0% CPU when idle, no UI and nothing in the Dock or the
menu bar.

Website: <https://indapublic.github.io/tildepin/>

## Install

**Download.** Get [TildePin.dmg](https://github.com/indapublic/tildepin/releases/latest/download/TildePin.dmg),
drag TildePin to Applications and open it once. From then on it starts at every
login (macOS 13 or later; on older versions install it with Homebrew).

**Homebrew.**

```sh
brew tap indapublic/tildepin https://github.com/indapublic/tildepin
brew trust --formula indapublic/tildepin/tildepin   # Homebrew 7+ asks to trust third-party taps
brew install indapublic/tildepin/tildepin
brew services start tildepin
```

From a clone of this repository, `make install` does the same. Use one way or
the other, not both.

Either way, then enable TildePin in System Settings > Privacy & Security >
Accessibility.

## Commands

Run from a clone of this repository; `make help` prints the same list.

| Command | What it does |
| --- | --- |
| `make install` | Install from the Homebrew tap and start the service |
| `make update` | Upgrade to the latest release and restart the service |
| `make uninstall` | Stop the service and uninstall |
| `make start`, `make stop`, `make restart` | Control the service |
| `make status` | Show the service state and memory footprint |
| `make logs` | Follow the log |
| `make` | Build `TildePin.app` |
| `make test` | Run the remap tests |
| `make keys` | Print the keycode each key sends, for checking a keyboard |
| `make lint` | Run the clang static analyzer and shellcheck |
| `make release VERSION=x.y.z` | Build, sign and notarize, tag, publish the GitHub release and update the formula (needs the Developer ID, see `tools/release.sh`) |
| `make icon` | Render the icon PNGs from `assets/icon.svg` and `assets/icon-small.svg` (needs Google Chrome) |
| `make clean` | Remove build products |

Without a clone, the same is available through `brew upgrade tildepin`,
`brew uninstall tildepin` and `brew services start|stop|restart tildepin`.

Releases are universal builds signed with a Developer ID and notarized, so the
Accessibility permission survives updates. Builds from source (`--HEAD`,
`make`) are signed ad hoc, and macOS asks for the permission again after each
of them; TildePin removes the stale entry itself, so you only need to switch
it on.

To remove a copy installed from the DMG, run
`/Applications/TildePin.app/Contents/MacOS/TildePin --unregister` (it stops
TildePin and takes it out of Login Items), then move TildePin to the Trash.
Turning TildePin off in System Settings > General > Login Items stops it too.

After uninstalling either way, remove TildePin from System Settings > Privacy &
Security > Accessibility with the − button.

## How it works

An ISO keyboard has an extra key between the left Shift and Z, and puts `/~ on
it. The key under Esc is the section key, §/±. TildePin swaps the two
virtual keycodes — 10 for the section key, 50 for grave — in a `CGEventTap`, so
the top-left key types `/~ and the key left of Z types §/±.

It swaps keycodes, not characters, and only for key-down and key-up events.
Modifiers, key repeat and dead keys keep working, nothing is typed for you, and
the result is the same in every input source, Russian and Ukrainian included.

`make keys` builds the same program with a diagnostic mode that prints the
virtual keycode of every key you press, which is handy for checking a keyboard
whose extra key does something else.

## Logs and bug reports

TildePin keeps a short log at `~/Library/Logs/TildePin.log`, however it was
installed. Each line has a timestamp; TildePin records when it starts (with its
version and the macOS version) and when the event tap comes back after a timeout.
Nothing else: no key codes, no app names, no files, nothing from the network.
Past 512 KB the log moves to `TildePin.old.log` and starts over.

To report a problem, [open an issue](https://github.com/indapublic/tildepin/issues/new/choose)
and attach the log. This shows it in Finder:

```sh
open -R ~/Library/Logs/TildePin.log
```

Console.app lists it too, under Log Reports.

## License

MIT
