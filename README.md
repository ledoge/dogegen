## [Download latest release](https://github.com/ledoge/dogegen/releases/latest/download/release.zip)
([Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) required, which you likely already have installed)

# About

This is a minimal command-line test pattern generator for Windows 10/11 with support for 8 and 10 bit color, both in SDR (BT.709) and HDR (PQ BT.2100). It can be used with DisplayCAL, Calman and ColourSpace via the "Resolve" XML protocol, and with HCFR by acting as a PGenerator.

# Usage

Running the exe will give you two windows: A console window for entering commands (see below) and a D3D11 window for rendering patterns, which can be freely resized and switched into borderless fullscreen with Alt+Enter.

For automation or creating shortcuts, commands can be supplied as command line arguments. Example: `dogegen.exe "maxcll 1000" "resolve 127.0.0.1"`

## Use with DisplayCAL

In DisplayCAL, select "Resolve" under the Display dropdown and disable "Override minimum display update delay". When prompted to connect the TPG to DisplayCAL, enter the following command:

```
resolve
```

This will make it act as an HDR TPG. Ensure that you have Windows HDR enabled, as this is not checked. Also, if you want to render patterns in SDR, you can specify `resolve_sdr` instead.

If DisplayCAL is running on a different machine, you can enter its IP as an argument. Additionally, a window size can be specified to override the coordinates specified by DisplayCAL. This is useful if you want to perform all measurements with a centered 10% window as commonly done by other software:

```
resolve 192.168.1.23 10
```

Either an IP address or a window size can be specified as a single argument, but if both are used, the IP address must come first. Optionally, the IP address can be followed by `:` and a port number to connect to (default is 20002).

## Use with Calman and ColourSpace

Please see the manuals of the respective software on how to prepare them for use with Resolve as TPG. The same `resolve` command as with DisplayCAL is then used to establish the connection. 8 and 10 bit patterns are supported.

## Use with HCFR

Enter this command:

```
pgen
```

To make it act as a PGenerator with HDR output. Like with `resolve`, you must ensure that Windows HDR is enabled or alternatively run `pgen_sdr` for SDR pattern rendering.

As this is an extremely limited implementation, specific settings are required in HCFR for it to work: In the Generator configuration menu, select "Raspberry Pi" and make sure that the "rPi user pattern" and "Display triplets" options are disabled. The output range under GDI options can be freely chosen depending on your needs, but for PC use you likely want 0 - 255.

As soon as you render a pattern in HCFR, it will try to discover a PGenerator instance via a UDP broadcast. Since there is no option to manually specify an IP, you must be connected to a network for this to work, even if you are running it on the same machine.

Optionally, an 8 bit RGB triplet can be specified, which will be displayed as a full field "passive" pattern whenever HCFR is not displaying any patches. Example:

```
pgen 100 100 100
```

## Setting HDR metadata

If you require HDR metadata (specifically maxCLL, maxFALL and maxDML) to be sent, you can enter the following command first:

```
maxcll 1000
```

The number is in nits and must be between 0 and 10000. Note that this uses the DXGI [SetHDRMetadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata) function, which does not guarantee that the metadata is actually sent to the display. On my Windows 10 machine with an NVIDIA GPU, it seems to be sent whenever the TPG window is fullscreened.

A value of `-1` unsets the metadata, though I don't know what the effect of that is supposed to be.

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
