# FNK0104B Case

OpenSCAD model for a two-part 25 mm deep case:

- `FNK0104B_case.scad` - parametric source
- `build/FNK0104B-main-case.stl` - main case with LCD window
- `build/FNK0104B-lid.stl` - screw-on lid
- `build/FNK0104B-snap-fit-lid.stl` - lid variant without screw holes
- `build/FNK0104B-assembly-preview.png` - quick preview render

## Assumptions

The defaults are inferred from the supplied images:

- Board: `88 mm x 50 mm`, rotated so the USB-side edge is on the left
- External case body: `110 mm x 72.6 mm`
- Internal case body: `106 mm x 68.6 mm`
- LCD visible opening: `57.2 mm x 42.8 mm`, based on the `57.6 mm x 43.2 mm` active screen area with a `0.2 mm` underlap per edge to hide the bezel
- LCD bevel: `1.2 mm` wide by `0.8 mm` deep around the visible opening
- Display front face to PCB mounting plane: `3.6 mm`
- Board mounting holes: `3 mm`, with `78.5 mm x 42 mm` measured center spacing rotated with the board
- Total closed case depth: `35 mm`
- Front wall thickness: `1.2 mm`
- Internal long-side cavity: `106 mm`
- External long-side case length: `110 mm`
- Top-edge legs: four square-cornered `15 mm x 10 mm x 5 mm` legs; front pair at squared-off top corners, rear pair inset `10 mm` from each side edge
- Side arms: two `15 mm x 15 mm x 15 mm` arms, one-third down from the top edge
- USB cutout: centered on the left short edge as a `12 mm x 7 mm` hole, with a `12.6 mm` wide open-sided channel, side guide walls, a matching inner-wall hole that abuts the board plane, and a `6 mm` diameter through-hole in the lid aligned with the main-case channel plus a `2.5 mm` wide edge opening into that hole
- Long-edge internal connector clearance pockets: `96 mm x 14 mm`, leaving `0.8 mm` outside wall

## Useful Measurements To Confirm

Before a final print, check these against the actual board and update the named parameters near the top of `FNK0104B_case.scad` if needed:

- Exact board width and height
- Mounting hole center-to-center spacing
- LCD glass/window offset from the board center
- Actual distance from the display front face to the PCB mounting surface
- USB connector center position and required cutout size
- Required side clearance for the long-edge connectors

## Export

```sh
openscad -D 'part="main"' -o build/FNK0104B-main-case.stl FNK0104B_case.scad
openscad -D 'part="lid"'  -o build/FNK0104B-lid.stl       FNK0104B_case.scad
openscad -D 'part="snap-fit-lid"' -o build/FNK0104B-snap-fit-lid.stl FNK0104B_case.scad
```
