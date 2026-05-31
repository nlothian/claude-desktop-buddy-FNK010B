// FNK0104B / 2.8 inch ILI9341-style display case
// Units: millimeters
//
// Export with:
//   openscad -D 'part="main"' -o build/FNK0104B-main-case.stl FNK0104B_case_v2.scad
//   openscad -D 'part="snap-fit-lid"' -o build/FNK0104B-snap-fit-lid.stl FNK0104B_case_v2.scad

$fn = 64;

part = "assembly"; // "main", "snap-fit-lid", "assembly"

// Overall enclosure
case_depth = 35;          // outside depth when the lid is installed
wall = 2.0;
front_wall = 1.2;
lid_thickness = 2.4;
lid_lip_depth = 1.8;
lid_lip_wall = 1.5;
lid_lip_clearance = 0.25;
corner_radius = 4.0;
lid_grille_d = 30.0;
lid_grille_slot_w = 2.0;
lid_grille_slot_pitch = 5.0;
lid_grille_slot_count = 5;
lid_grille_angle = 0.0;

// Cantilever snap-fit tabs on the lid lip, with matching pockets in the case
// long walls. Hooks face outward into the leg-side and away-from-legs walls.
snap_tab_w = 12.0;        // along case width
snap_tab_len = 7.0;       // cantilever length from the lid underside
snap_tab_thickness = 1.2; // flexing beam thickness
snap_hook_depth = 0.8;    // outward catch protrusion
snap_hook_h = 2.0;
snap_slot_w = 0.8;
snap_slot_clearance = 0.4;
snap_catch_wall_remaining = 0.55;

// External legs on the top edge.
leg_len = 15.0;           // outward from the top edge
leg_w = 10.0;             // across the case width
leg_depth = 5.0;          // along the case depth
rear_leg_depth = 7.0;     // thicker toward the screen/front side for cleaner printing
rear_leg_inset = 10.0;    // inset from each long edge

// External arms on the side edges, measured from the top edge.
arm_len = 22.0;           // outward from each long edge
arm_w = 20.3;             // along the case height, centered on the USB cutout
arm_depth = 20.0;         // along the case depth
arm_wall = wall;

// Board dimensions, rotated so the USB-side edge is on the left.
board_w = 88.0;
board_h = 50.0;
board_thickness = 1.6;
board_clearance = 1.0;
case_height = 72.6;       // outside height of the main case body

// Front LCD opening. The drawing gives a 43.20 x 57.60 active area inside a
// 50.00 x 69.20 bezel/block outline. The module is rotated CCW here so the USB
// side is left; the front opening reveals the active screen only.
board_away_from_legs_edge_y = -case_height / 2 + wall;
board_center_y = board_away_from_legs_edge_y + board_h / 2;
lcd_bezel_portrait_w = 50.0;
lcd_bezel_portrait_h = 69.2;
lcd_active_portrait_w = 43.2;
lcd_active_portrait_h = 57.6;
lcd_active_top_margin = 3.05;
screen_reveal_inset = 0.2; // underlaps the active area so the black bezel stays hidden
screen_cutout_w = lcd_active_portrait_h - 2 * screen_reveal_inset;
screen_cutout_h = lcd_active_portrait_w - 2 * screen_reveal_inset;
screen_cutout_r = 0.8;
screen_bevel = 1.2;
screen_bevel_depth = 0.8;
screen_center_x = 0.0;
screen_center_y = board_center_y;

// Bezel/reference placement relative to the centered active LCD. In portrait
// the active area is 2.75 mm closer to the top edge; after rotation that offset
// becomes horizontal.
lcd_active_offset_y_portrait =
    lcd_active_top_margin + lcd_active_portrait_h / 2 - lcd_bezel_portrait_h / 2;
screen_active_offset_x_from_bezel = -lcd_active_offset_y_portrait;
screen_bezel_w = lcd_bezel_portrait_h;
screen_bezel_h = lcd_bezel_portrait_w;
screen_bezel_center_x = screen_center_x - screen_active_offset_x_from_bezel;
screen_bezel_center_y = screen_center_y;
board_center_x = screen_bezel_center_x;
board_usb_side_edge_x = board_center_x - board_w / 2;
case_center_x = screen_center_x;
case_width = 2 * (case_center_x - board_usb_side_edge_x + wall); // USB-side inner wall aligns to the board; opposite wall centers the screen

screen_stack_thickness = 2.3;
display_front_to_board_mount = front_wall + screen_stack_thickness + 0.1;

// Board mounting holes: measured as 78.5 mm x 42 mm, then rotated with board.
board_hole_d = 3.0;
board_hole_x = 39.25;
board_hole_y = 21.0;
board_post_d = 6.2;
board_insert_hole_d = 3.1;
board_insert_depth = 3.0;
board_screw_pilot_front_skin = 0.8;

// Side openings
usb_cutout_w = 12.0;
usb_cutout_h = 9.0;
usb_cutout_z = 8.6;        // center height from front face
usb_cutout_y = board_center_y;
sd_slot_from_non_usb_edge = 28.5;
sd_slot_w = 16.0;
sd_slot_h = 3.0;
sd_slot_z = display_front_to_board_mount + board_thickness + sd_slot_h / 2 + 2.0;
mic_hole_d = 3.0;
mic_hole_offset_from_leg_side_standoff = 5.0;
mic_hole_y = board_center_y + board_hole_y - mic_hole_offset_from_leg_side_standoff;
mic_hole_z = 7.0;

main_depth = case_depth - lid_thickness;
outer_w = case_width;
outer_h = case_height;
inner_w = outer_w - 2 * wall;
inner_h = outer_h - 2 * wall;
arm_y = usb_cutout_y;

module rounded_rect(w, h, r) {
    offset(r = r)
        square([w - 2 * r, h - 2 * r], center = true);
}

module rounded_box(w, h, d, r) {
    linear_extrude(height = d)
        rounded_rect(w, h, r);
}

module rounded_cutout(w, h, d, r) {
    linear_extrude(height = d)
        rounded_rect(w, h, r);
}

module at_corners(x, y) {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * x, sy * y, 0])
            children();
}

module main_shell() {
    translate([case_center_x, 0, 0])
        difference() {
            rounded_box(outer_w, outer_h, main_depth, corner_radius);

            translate([0, 0, front_wall])
                rounded_box(
                    inner_w,
                    inner_h,
                    main_depth - front_wall + 0.2,
                    max(corner_radius - wall, 0.5)
                );
        }
}

module board_posts() {
    translate([board_center_x, board_center_y, 0]) {
        at_corners(board_hole_x, board_hole_y) {
            difference() {
                cylinder(d = board_post_d, h = display_front_to_board_mount);
                translate([0, 0, board_screw_pilot_front_skin])
                    cylinder(
                        d = board_insert_hole_d,
                        h = board_insert_depth + 0.2
                    );
            }
        }
    }
}

module usb_side_legs() {
    leg_y = outer_h / 2 + leg_len / 2;

    // Front pair, flush with the front plane and aligned to the case corners.
    for (sx = [-1, 1]) {
        translate([case_center_x + sx * (outer_w / 2 - leg_w / 2), leg_y, 0])
            linear_extrude(height = leg_depth)
                square([leg_w, leg_len], center = true);
    }

    // Rear pair, aligned with the lid side and inset from the long edges.
    for (sx = [-1, 1]) {
        translate([case_center_x + sx * (outer_w / 2 - rear_leg_inset - leg_w / 2), leg_y, main_depth - rear_leg_depth])
            linear_extrude(height = rear_leg_depth)
                square([leg_w, leg_len], center = true);
    }
}

module front_leg_corner_fill() {
    for (sx = [-1, 1]) {
        translate([
            case_center_x + sx * (outer_w / 2 - corner_radius / 2),
            outer_h / 2 - corner_radius / 2,
            leg_depth / 2
        ])
            cube([corner_radius, corner_radius, leg_depth], center = true);
    }
}

module side_arm_solid(sx, arm_y) {
    translate([
        case_center_x + sx * (outer_w / 2 + arm_len / 2),
        arm_y,
        arm_depth / 2
    ])
        cube([arm_len, arm_w, arm_depth], center = true);
}

module usb_side_arm_open_bottom_back(arm_y) {
    x_center = case_center_x - outer_w / 2 - arm_len / 2;
    y_away_from_legs = arm_y - arm_w / 2;
    y_leg_side = arm_y + arm_w / 2;

    // This arm sits next to the USB hole. It is open on the outer wall and
    // lid-facing back side, with no outer-wall rim.
    union() {
        translate([x_center, y_away_from_legs + arm_wall / 2, arm_depth / 2])
            cube([arm_len, arm_wall, arm_depth], center = true);

        translate([x_center, y_leg_side - arm_wall / 2, arm_depth / 2])
            cube([arm_len, arm_wall, arm_depth], center = true);

        translate([x_center, arm_y, arm_wall / 2])
            cube([arm_len, arm_w, arm_wall], center = true);
    }
}

module side_arms() {
    usb_side_arm_open_bottom_back(arm_y);
    side_arm_solid(1, arm_y);
}

module screen_cutout() {
    translate([screen_center_x, screen_center_y, -0.1])
        rounded_cutout(screen_cutout_w, screen_cutout_h, front_wall + 0.4, screen_cutout_r);

    translate([screen_center_x, screen_center_y, 0])
        hull() {
            translate([0, 0, -0.12])
                rounded_cutout(
                    screen_cutout_w + 2 * screen_bevel,
                    screen_cutout_h + 2 * screen_bevel,
                    0.02,
                    screen_cutout_r + screen_bevel
                );

            translate([0, 0, screen_bevel_depth])
                rounded_cutout(screen_cutout_w, screen_cutout_h, 0.02, screen_cutout_r);
        }
}

module usb_cutout() {
    translate([case_center_x - outer_w / 2, usb_cutout_y, usb_cutout_z])
        cube([wall + 4.0, usb_cutout_w, usb_cutout_h], center = true);
}

module sd_card_slot_cutout() {
    board_non_usb_edge_x = board_center_x + board_w / 2;
    sd_slot_center_x =
        board_non_usb_edge_x - sd_slot_from_non_usb_edge - sd_slot_w / 2;

    translate([sd_slot_center_x, -outer_h / 2, sd_slot_z])
        cube([sd_slot_w, wall + 4.0, sd_slot_h], center = true);
}

module microphone_cutout() {
    translate([case_center_x + outer_w / 2, mic_hole_y, mic_hole_z])
        rotate([0, 90, 0])
            cylinder(d = mic_hole_d, h = wall + 4.0, center = true);
}

module snap_catch_cutouts() {
    catch_depth = wall - snap_catch_wall_remaining;
    catch_h = snap_hook_h + 0.6;
    catch_w = snap_tab_w + 2 * snap_slot_clearance;
    catch_z = main_depth - snap_tab_len + snap_hook_h / 2;

    for (sy = [-1, 1]) {
        translate([
            case_center_x,
            sy * (inner_h / 2 + catch_depth / 2 - 0.05),
            catch_z
        ])
            cube([catch_w, catch_depth + 0.1, catch_h], center = true);
    }
}

module main_case() {
    difference() {
        union() {
            main_shell();
            board_posts();
            usb_side_legs();
            front_leg_corner_fill();
            side_arms();
        }

        screen_cutout();
        usb_cutout();
        sd_card_slot_cutout();
        microphone_cutout();
        snap_catch_cutouts();
    }
}

module snap_lip_relief_slots() {
    slot_y = inner_h / 2 - lid_lip_clearance - lid_lip_wall / 2;
    slot_depth = lid_lip_wall + 2 * snap_hook_depth;

    for (sy = [-1, 1], sx = [-1, 1]) {
        translate([
            case_center_x + sx * (snap_tab_w / 2 + snap_slot_w / 2),
            sy * slot_y,
            -snap_tab_len / 2
        ])
            cube([snap_slot_w, slot_depth, snap_tab_len + 0.2], center = true);
    }
}

module snap_lid_tabs() {
    lip_outer_y = inner_h / 2 - lid_lip_clearance;
    tab_y = lip_outer_y - snap_tab_thickness / 2;

    for (sy = [-1, 1]) {
        translate([
            case_center_x,
            sy * tab_y,
            -snap_tab_len / 2
        ])
            cube([snap_tab_w, snap_tab_thickness, snap_tab_len], center = true);

        hull() {
            translate([
                case_center_x,
                sy * (lip_outer_y + 0.025),
                -snap_tab_len
            ])
                cube([snap_tab_w, 0.05, 0.05], center = true);

            translate([
                case_center_x,
                sy * (lip_outer_y + snap_hook_depth / 2),
                -snap_tab_len + snap_hook_h
            ])
                cube([snap_tab_w, snap_hook_depth, 0.05], center = true);
        }
    }
}

module lid_grille_cutouts() {
    translate([case_center_x, 0, lid_thickness / 2])
        intersection() {
            cylinder(d = lid_grille_d, h = lid_thickness + 0.4, center = true);

            rotate([0, 0, lid_grille_angle])
                for (i = [-(lid_grille_slot_count - 1) / 2 : (lid_grille_slot_count - 1) / 2]) {
                    translate([0, i * lid_grille_slot_pitch, 0])
                        cube([
                            lid_grille_d * 1.2,
                            lid_grille_slot_w,
                            lid_thickness + 0.6
                        ], center = true);
                }
        }
}

module lid_raw() {
    union() {
        difference() {
            union() {
                translate([case_center_x, 0, 0])
                    rounded_box(outer_w, outer_h, lid_thickness, corner_radius);

                translate([case_center_x, 0, -lid_lip_depth])
                    difference() {
                        rounded_box(
                            inner_w - 2 * lid_lip_clearance,
                            inner_h - 2 * lid_lip_clearance,
                            lid_lip_depth,
                            max(corner_radius - wall - lid_lip_clearance, 0.5)
                        );

                        translate([0, 0, -0.1])
                            rounded_box(
                                inner_w - 2 * lid_lip_clearance - 2 * lid_lip_wall,
                                inner_h - 2 * lid_lip_clearance - 2 * lid_lip_wall,
                                lid_lip_depth + 0.2,
                                max(corner_radius - wall - lid_lip_clearance - lid_lip_wall, 0.5)
                            );
                    }

                snap_lid_tabs();
            }

            snap_lip_relief_slots();
            lid_grille_cutouts();
        }
    }
}

module lid_for_print() {
    translate([0, 0, lid_lip_depth])
        lid_raw();
}

module board_reference() {
    board_z = display_front_to_board_mount + board_thickness / 2;

    color([0.02, 0.22, 0.08, 0.45])
        translate([board_center_x, board_center_y, board_z])
            cube([board_w, board_h, board_thickness], center = true);

    color([0.01, 0.01, 0.01, 0.45])
        translate([screen_bezel_center_x, screen_bezel_center_y, front_wall])
            linear_extrude(height = screen_stack_thickness)
                rounded_rect(screen_bezel_w, screen_bezel_h, screen_cutout_r);

    color([0.02, 0.02, 0.02, 0.65])
        translate([screen_center_x, screen_center_y, front_wall])
            linear_extrude(height = screen_stack_thickness + 0.05)
                rounded_rect(screen_cutout_w, screen_cutout_h, screen_cutout_r);

    color([0.85, 0.85, 0.85, 0.8])
        translate([board_center_x - board_w / 2 - 1.5, board_center_y, usb_cutout_z])
            cube([5, 10, 4], center = true);
}

module assembly() {
    color([0.88, 0.88, 0.84, 1.0])
        main_case();

    translate([0, 0, main_depth])
        color([0.70, 0.70, 0.66, 0.65])
            lid_raw();

    board_reference();
}

if (part == "main") {
    main_case();
} else if (part == "snap-fit-lid") {
    lid_for_print();
} else {
    assembly();
}
