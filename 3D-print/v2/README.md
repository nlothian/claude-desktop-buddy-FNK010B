# FNK0104B Case

OpenSCAD model for a two-part 25 mm deep case:

- `FNK0104B_case_v2.scad` - parametric source
- `build/FNK0104B-main-case.stl` - main case with LCD window
- `build/FNK0104B-snap-fit-lid.stl` - snap-fit lid
- `build/FNK0104B-assembly-preview.png` - quick preview render

## Assumptions

The defaults are inferred from the supplied images:

- Board: `88 mm x 50 mm`, rotated so the USB-side edge is on the left, the USB-side edge abuts the inner short wall, and the away-from-legs edge abuts the inner long wall
- External case body: `97.5 mm x 72.6 mm`
- Internal case body: `93.5 mm x 68.6 mm`
- LCD visible opening: `57.2 mm x 42.8 mm`, based on the `57.6 mm x 43.2 mm` active screen area with a `0.2 mm` underlap per edge to hide the bezel
- LCD bevel: `1.2 mm` wide by `0.8 mm` deep around the visible opening
- Display front face to PCB mounting plane: `3.6 mm`
- Board mounting holes: `3 mm`, with `78.5 mm x 42 mm` measured center spacing rotated with the board
- Lid screw support bosses and matching lid screw holes are removed
- Total closed case depth: `35 mm`
- Front wall thickness: `1.2 mm`
- Internal long-side cavity: `93.5 mm`
- External long-side case length: `97.5 mm`
- Top-edge legs: four square-cornered `15 mm x 10 mm` legs; front pair are `5 mm` deep at the front plane, rear pair are `7 mm` deep with the extra thickness extending toward the screen/front side
- Side arms: two `22 mm x 20.3 mm x 20 mm` arms, centered on the USB cutout; the USB-side arm is open on the lid-facing back side and outer wall
- USB cutout: `12 mm x 7 mm` hole on the USB-side short wall, centered `9.6 mm` from the screen/front face and aligned to the USB connector on the board; the screen is centered in the front face by moving the opposite short wall outward
- MicroSD slot: `16 mm x 3 mm` hole through the away-from-legs long wall, starting `28.5 mm` from the non-USB board edge and positioned `2 mm` farther from the screen face than the PCB back-surface reference
- Microphone hole: `3 mm` round hole through the opposite-side short wall, positioned `5 mm` toward the away-from-legs side from the leg-side screen standoff and centered `7 mm` above the screen surface
- Long walls use the same `2 mm` wall thickness throughout, with no internal thinning pockets
- Snap-fit lid uses two `12 mm` wide cantilever tabs on the lid lip, one each on the leg-side and away-from-legs long walls, with matching internal catch pockets in the case walls
- Lid grille: centered `30 mm` circular grille using five horizontal `2 mm` wide straight-through slots, printable without support

## Useful Measurements To Confirm

Before a final print, check these against the actual board and update the named parameters near the top of `FNK0104B_case_v2.scad` if needed:

- Exact board width and height
- Mounting hole center-to-center spacing
- LCD glass/window offset from the board center
- Actual distance from the display front face to the PCB mounting surface
- USB connector center position and required cutout size
- Required side clearance for the long-edge connectors

## Export

```sh
openscad -D 'part="main"' -o build/FNK0104B-main-case.stl FNK0104B_case_v2.scad
openscad -D 'part="snap-fit-lid"' -o build/FNK0104B-snap-fit-lid.stl FNK0104B_case_v2.scad
```
