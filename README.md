## [Download latest release](https://github.com/ledoge/dogegen/releases/latest/download/release.zip)

# About

This is a minimal command-line test pattern generator for Windows 10+ with support for 8 and 10 bit color, both in SDR (BT.709) and HDR (PQ BT.2100). The "Resolve" XML protocol over TCP, as implemented as DisplayCAL and Calman, is also supported.

# Usage

Running the exe will give you a D3D11 window, which can be freely resized and switched into borderless fullscreen with Alt+Enter, in addition to a console window for entering commands.

## Use with DisplayCAL

In DisplayCAL, select "Resolve" under the Display dropdown and disable "Override minimum display update delay". When prompted to connect the TPG to DisplayCAL, simply enter the following command:

```
resolve 127.0.0.1
```

This will make it act as an HDR TPG. Ensure that you have Windows HDR enabled, as this is not checked. Also, if you want to render patterns in SDR, you can specify `resolve_sdr` instead.

Of course, you can specify a different IP address if DisplayCAL is not running on the local machine. Additionally, a window size can be specified to override the coordinates specified by DisplayCAL. This is useful if you want to perform all measurements with a centered 10% window as commonly done by other software:

```
resolve 127.0.0.1 10
```

## Manual pattern generation

First, switch the TPG into the desired bit depth + HDR mode, e.g.

```
mode 10_hdr
```

The supported values are `8` (default), `8_hdr`, `10`, and `10_hdr`.

Patterns are specified via a `;`-separated list of `window` or `draw` commands. Example (assuming a 10 bit mode):

```
window 100 512 512 512; window 10 1023 1023 1023
```

This draws a full field background with code value `(512, 512, 512)`, followed by a 10% peak white window.

The `draw` command requires more arguments and can be used to generate rectangles with arbitrary coordinates and gradients. Example (assuming an 8 bit mode):

```
draw -1 1 1 -1 0 0 0 256 256 256 0 0 0 256 256 256 1
```

This draws a full field gradient with even spacing.

Specifying a new pattern overrides the old one, and entering nothing (i.e. just pressing Enter) results in the pattern being cleared.

There is also a `flicker` command, potentially useful for display testing. Use it at your own risk, as it could cause seizures or damage to your display. Example:

```
flicker 1
```

This alternates between the current pattern and black on every frame. The parameter specifies the number of black frames between every flashing of the pattern, with `0` disabling it again.

# Thanks

Special thanks to kevinmoran for their public domain D3D11 samples, which this is based on: https://github.com/kevinmoran/BeginnerDirect3D11
