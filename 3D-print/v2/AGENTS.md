Main file is FNK0104B_case_v2.scad

Shared case terminology:

- `front` means the display/screen face, at low Z.
- `back` means the lid-facing side, at high Z.
- `USB side` means the left short wall, at negative X.
- `opposite side` means the right short wall, at positive X.
- `leg side` means the edge with the external legs, at positive Y.
- `away-from-legs side` means the opposite edge, at negative Y.

Avoid using bare `top` or `bottom` for X/Y case features because it is ambiguous
between the rendered view, print orientation, and physical orientation. Prefer the
terms above. If the user says `top face` for a side-arm face, treat that as the
away-from-legs face unless they say otherwise.

Always rebuild build/FNK0104B-main-case.stl and build/FNK0104B-snap-fit-lid.stl and re-render FNK0104B-assembly-preview.png after changes.

There are images with measurements in ./input/*. If you aren't sure or need a more precise measurement always ask and don't guess.

Keep README.md up-to-date. 
