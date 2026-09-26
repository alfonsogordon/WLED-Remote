# WLED Remote

A pocket WLED controller for the **M5StickC Plus2**.

## Planned v0.1.0-alpha

- Phone-friendly Wi-Fi provisioning (no credentials in firmware)
- Automatic WLED discovery on the local network
- Live lamp state
- Individual and all-lights power control
- White mode
- Restore a default WLED preset
- Brightness control
- 30 min / 1 h / 2 h / 3 h sleep timers
- Animated, colourful M5StickC Plus2 UI
- Browser flashing with ESP Web Tools
- GitHub Actions firmware builds

## Hardware

- M5StickC Plus2

## Status

Early development. The first goal is a testable end-to-end build: provision Wi-Fi, discover WLED devices, display them, and control power/timers.

## Controls (initial design)

- Front button A: select / enter
- Side button B: next
- Hold A: quick power toggle
- Hold B: back

## Default colours

The `DEFAULT` action will recall a configurable WLED preset. The initial convention is **Preset 1**.

## License

To be selected before the first stable release.
