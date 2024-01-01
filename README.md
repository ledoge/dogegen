## [Download latest release](https://github.com/ledoge/dogegen/releases/latest/download/release.zip)

# About

This is a minimal command-line test pattern generator for Windows 10+ with support for 8 and 10 bit color, both in SDR (BT.709) and HDR (PQ BT.2100). For use with profiling/calibration software, the "Resolve" XML protocol over TCP as implemented by DisplayCAL and Calman, and a small subset of the PGenerator protocol as implemented by HCFR are supported.

# Usage

Running the exe will give you two windows: A console window for entering commands (see below) and a D3D11 window for rendering patterns, which can be freely resized and switched into borderless fullscreen with Alt+Enter.

## Use with DisplayCAL

In DisplayCAL, select "Resolve" under the Display dropdown and disable "Override minimum display update delay". When prompted to connect the TPG to DisplayCAL, enter the following command:

```
resolve 127.0.0.1
```

This will make it act as an HDR TPG. Ensure that you have Windows HDR enabled, as this is not checked. Also, if you want to render patterns in SDR, you can specify `resolve_sdr` instead.

If DisplayCAL is not running on the same machine, you can enter the corresponding IP instead. Additionally, a window size can be specified to override the coordinates specified by DisplayCAL. This is useful if you want to perform all measurements with a centered 10% window as commonly done by other software:

```
resolve 127.0.0.1 10
```

## Use with HCFR

Enter this command:

```
pgen
```

To make it act as a PGenerator with HDR output. Like with `resolve`, you must ensure that Windows HDR is enabled or alternatively run `pgen_sdr` for SDR pattern rendering.

As this is an extremely limited implementation, specific settings are required in HCFR for it to work: In the Generator configuration menu, select "Raspberry Pi" and make sure that the "rPi user pattern" and "Display triplets" options are disabled. The output range under GDI options can be freely chosen depending on your needs, but for PC use you likely want 0 - 255.

As soon as you render a pattern in HCFR, it will try to discover a PGenerator instance via a UDP broadcast. Since there is no option to manually specify an IP, you must be connected to a network for this to work, even if you are running it on the same machine.

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

This draws a full field background with code value `(512, 512, 512)`, with a 10% peak white window on top.

The `draw` command requires more arguments and can be used to generate rectangles with arbitrary coordinates and gradients. Example (assuming an 8 bit mode):

```
draw -1 1 1 -1 0 0 0 256 256 256 0 0 0 256 256 256 1
```

This draws a full field 0-255 grayscale gradient with even spacing.

Specifying a new pattern overrides the old one, and entering nothing (i.e. just pressing Enter) results in the pattern being cleared.

There is also a `flicker` command, potentially useful for display testing. Use it at your own risk, as it could cause seizures or damage to your display. Example:

```
flicker 1
```

This alternates between the current pattern and black on every frame. The parameter specifies the number of black frames between every flashing of the pattern, with `0` disabling it again.

# Thanks

Special thanks to kevinmoran for their public domain D3D11 samples, which this is based on: https://github.com/kevinmoran/BeginnerDirect3D11
