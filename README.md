## [Download latest release](https://github.com/ledoge/dogegen/releases/latest/download/release.zip)
([Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) required, which you likely already have installed)

# About

This is a minimal command-line test pattern generator for Windows 10/11 with support for 8 and 10 bit color, both in SDR (BT.709) and HDR (PQ BT.2100). It can be used with DisplayCAL, Calman and ColourSpace via the "Resolve" XML protocol, and with HCFR by acting as a PGenerator.

# Usage

Running the exe will give you two windows: A console window for entering commands (see below) and a D3D11 window for rendering patterns, which can be freely resized and switched into borderless fullscreen with Alt+Enter.

For automation or creating shortcuts, commands can be supplied as command line arguments. Example: `dogegen.exe "maxcll 1000" "resolve_hdr 127.0.0.1"`

## Use with DisplayCAL

In DisplayCAL, select "Resolve" under the Display dropdown and disable "Override minimum display update delay". Additionally, I would recommend enabling the "Black background" option (shown in the "Measurement area" window), as patterns will be rendered with a gray background otherwise. When prompted to connect the TPG to DisplayCAL, enter the following command:

```
resolve_hdr
```

This will make it act as an HDR TPG. Ensure that you have Windows HDR enabled, as this is not checked. If you want to render patterns in SDR, you can specify `resolve_sdr` instead.

If DisplayCAL is running on a different machine, you can enter its IP as an argument. Also, a window size can be specified to override the coordinates specified by DisplayCAL. This is useful if you want to perform all measurements with a centered 10% window as commonly done by other software:

```
resolve_hdr 192.168.1.23 10
```

You can omit either one of these arguments, i.e. only specify the IP address or the window size as a single argument. Optionally, the IP address can be followed by `:` and a port number to connect to (default is 20002).

## Use with Calman and ColourSpace

Please see the manuals of the respective software on how to prepare them for use with Resolve as TPG. The same `resolve_hdr` or `resolve_sdr` command as with DisplayCAL is then used to establish the connection. With ColourSpace, 8 and 10 bit patterns can be used (higher bit depths are not supported by Windows), while Calman will always send 10 bit values (despite its general lack of 10 bit color support).

## Use with HCFR

Enter this command:

```
pgen_hdr
```

To make it act as a PGenerator with HDR output. Like with `resolve_hdr`, you must ensure that Windows HDR is enabled or alternatively run `pgen_sdr` for SDR pattern rendering.

As this is an extremely limited implementation, specific settings are required in HCFR for it to work: In the Generator configuration menu, select "Raspberry Pi" and make sure that the "rPi user pattern" and "Display triplets" options are disabled. The output range under GDI options can be freely chosen depending on your needs, but for PC use you likely want 0 - 255.

As soon as you render a pattern in HCFR, it will try to discover a PGenerator instance via a UDP broadcast. Since there is no option to manually specify an IP, you must be connected to a network for this to work, even if you are running it on the same machine.

Optionally, an 8 bit RGB triplet can be specified, which will be displayed as a full field "passive" pattern whenever HCFR is not displaying any patches. Example:

```
pgen_hdr 100 100 100
```

## Setting HDR metadata

If you require HDR peak luminance metadata (MaxCLL, MaxFALL and MaxDML) to be sent, you can enter the following command first:

```
maxcll 1000
```

The number is in nits and must be between 0 and 10000. Note that this uses the DXGI [SetHDRMetadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata) function, which does not guarantee that the metadata is actually sent to the display. On my Windows 10 machine with an NVIDIA GPU, it seems to be sent whenever the TPG window is fullscreened.

A value of `-1` unsets the metadata, though I don't know what the effect of that is supposed to be.

For more granular control over the metadata, you can specify separate values for MaxCLL, MaxFALL and MaxDML:

```
maxcll 1000 200 4000
```

## Manual pattern generation

First, switch the TPG into the desired bit depth + HDR mode, e.g.

```
mode 10_hdr
```

The supported values are `8` (default), `8_hdr`, `10`, and `10_hdr`.

When you are in a 10 bit mode, you can use the `pluge` command to draw the (limited range) 4K UHDTV/HDR-TV PLUGE pattern specified in [BT.814-4](https://www.itu.int/rec/R-REC-BT.814-4-201807-I/en). `pluge_hdr` can be used to draw the HDR variant without being in an HDR mode.

Custom patterns are specified via a `;`-separated list of `window` or `draw` commands. Example (assuming a 10 bit mode):

```
window 100 512 512 512; window 10 1023 1023 1023
```

This draws a full field background with code value `(512, 512, 512)`, with a 10% peak white window on top.

The `draw` command requires more arguments and can be used to generate rectangles with arbitrary coordinates and colors. Example (assuming an 8 bit mode):

```
draw -1 1 0 0 255 255 255
```

This draws a white rectangle in the top left corner of the window. The rectangle's corner coordinates, given as the first 4 arguments, are Direct3D Normalized Device Coordinates (i.e. top left of the window is `-1 1`, center is `0 0`, bottom right is `1 -1`).

Patterns with gradients can be generated using the extended version of the command, which takes a color for each corner and a quantization parameter to control the step size:

```
draw -1 1 1 -1 0 0 0 256 256 256 0 0 0 256 256 256 1
```

This draws a full field 0-255 grayscale gradient with even spacing. Note that the high level is specified as 256 because the quantization is implemented by rounding down to the nearest multiple of the quantization parameter. The coordinates extend half a pixel beyond the sampled pixel centers in each direction, so the rightmost pixels will assume a value slightly below 256 before rounding down.

Specifying a new pattern overrides the old one, and entering nothing (i.e. just pressing Enter) results in the pattern being cleared.

There is also a `flicker` command, potentially useful for display testing. Use it at your own risk, as it could cause seizures or damage to your display. Example:

```
flicker 1
```

This alternates between the current pattern and black on every frame. The parameter specifies the number of black frames between every flashing of the pattern, with `0` disabling it again.

## Debugging

You can enable the `debug` option to print the commands received over the network:

```
debug 1
```

If you are running into an issue where a pattern is not rendering as expected, please include the corresponding command(s) when filing an issue.

# Output accuracy

To be able to achieve accurate "bit-perfect" output, you should ensure these requirements are met:
* Set the GPU to full range RGB output, with the same bit depth as the patterns.
* Ensure that no VCGT calibration is loaded and that all GPU color settings are at their defaults. For NVIDIA GPUs, this can be done by enabling the "Override to reference mode" setting.
* Make sure GPU dithering is disabled. This can be done using programs such as [novideo_srgb](https://github.com/ledoge/novideo_srgb) (for NVIDIA GPUs only) or [ColorControl](https://github.com/Maassoft/ColorControl).
* Have the TPG window fullscreened. Not necessarily required, depending on the configuration, but should increase the chances of the TPG window bypassing the compositor and being presented directly to the display. 10 bit and/or HDR values cannot be rendered accurately if this does not happen.

Even when these requirements are met, the output might not be accurate. I don't have a signal analyzer/capture card myself to check the accuracy on my system, but if you do, please share any results.

# Thanks

Special thanks to kevinmoran for their public domain D3D11 samples, which this is based on: https://github.com/kevinmoran/BeginnerDirect3D11

Credit to Light Illusion for developing the XML Protocol as used by Resolve for TPG operation
