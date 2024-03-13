# sd-inhibit-bridge

A daemon implementing the
[Idle Inhibition D-Bus API](https://specifications.freedesktop.org/idle-inhibit-spec/latest/)
that forwards inhibitor locks to
[systemd-logind](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html).

This tool has no dependencies other than systemd.
It's intended to be used in configurations that don't provide their own
implementation, like with standalone Wayland compositors such as
[Sway](https://github.com/swaywm/sway).

## How to use

1. Enable the service: `systemctl --user enable --now sd-inhibit-bridge.service`
2. Trigger an idle inhibitor (e.g. watch a video in Firefox)
3. Inspect active inhibitors: `systemd-inhibit --list`

Note: a logind-aware idle manager
(such as [swayidle](https://github.com/swaywm/swayidle))
is required to honor the idle inhibitors.

## Install from source

```sh
meson setup --prefix=/usr/local build
meson install -C build
```

## Acknowledgements

- [bdwalton/inhibit-bridge](https://github.com/bdwalton/inhibit-bridge) -
the project this was based on
- [nachtimwald.com](https://nachtimwald.com/2020/03/06/generic-hashtable-in-c/) -
C hash table implementation
