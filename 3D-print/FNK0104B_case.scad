// FNK0104B / 2.8 inch ILI9341-style display case
// Units: millimeters
//
// Export with:
//   openscad -D 'part="main"' -o build/FNK0104B-main-case.stl FNK0104B_case.scad
//   openscad -D 'part="lid"'  -o build/FNK0104B-lid.stl       FNK0104B_case.scad

$fn = 64;

part = "assembly"; // "main", "lid", "assembly"

// Overall enclosure
case_depth = 35;          // outside depth when the lid is installed
wall = 2.0;
front_wall = 1.2;
lid_thickness = 2.4;
lid_lip_depth = 1.8;
lid_lip_wall = 1.5;
lid_lip_clearance = 0.25;
corner_radius = 4.0;

// External legs on the top edge.
leg_len = 15.0;           // outward from the top edge
leg_w = 10.0;             // across the case width
leg_depth = 5.0;          // along the case depth
rear_leg_inset = 10.0;    // inset from each long edge

// External arms on the side edges, measured from the top edge.
arm_len = 15.0;           // outward from each long edge
arm_w = 15.0;             // along the case height
arm_depth = 15.0;         // along the case depth
arm_from_top = 1 / 3;

// Board dimensions, rotated so the USB-side edge is on the left.
board_w = 88.0;
board_h = 50.0;
board_thickness = 1.6;
board_clearance = 1.0;
case_width = 110.0;       // outside width of the main case body
case_height = 72.6;       // outside height of the main case body

// Front LCD opening. The drawing gives a 43.20 x 57.60 active area inside a
// 50.00 x 69.20 bezel/block outline. The module is rotated CCW here so the USB
// side is left; the front opening reveals the active screen only.
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
screen_center_y = 0.0;

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
board_center_y = screen_bezel_center_y;

screen_stack_thickness = 2.3;
display_front_to_board_mount = front_wall + screen_stack_thickness + 0.1;

// Board mounting holes: measured as 78.5 mm x 42 mm, then rotated with board.
board_hole_d = 3.0;
board_hole_x = 39.25;
board_hole_y = 21.0;
board_post_d = 6.2;
board_screw_pilot_d = 2.1; // M2/M2.2 self-tap pilot; enlarge for heat-set inserts
board_screw_pilot_front_skin = 0.8;

// Lid screws use separate bosses just outside the board footprint.
lid_screw_d = 2.7;         // clearance hole in lid for M2.5 screws
lid_pilot_d = 2.1;         // pilot in main case boss
lid_boss_d = 6.4;
lid_screw_margin = 5.6;    // from outside edge to lid screw center
counterbore_d = 5.2;
counterbore_depth = 1.2;

// Side openings
usb_cutout_w = 12.0;
usb_cutout_h = 7.0;
usb_cutout_z = 7.6;        // center height from front face
usb_cutout_y = 0.0;
usb_channel_clearance = 0.6;
usb_channel_board_overlap = 1.0;
usb_channel_outer_overlap = 3.0;
usb_channel_wall_thickness = 1.2;
usb_cable_d = 4.0;
usb_lid_hole_d = 6.0;
usb_lid_edge_opening = 2.5;
usb_retainer_wall_overlap = 0.1;

// Internal connector clearance pockets on the long edges. These do not break
// through the outside wall.
long_edge_clearance_len = 96.0;
long_edge_clearance_h = 14.0;
long_edge_clearance_z = 14.0; // center height from front face
long_edge_remaining_wall = 0.8;
long_edge_overlap = 0.2;

main_depth = case_depth - lid_thickness;
outer_w = case_width;
outer_h = case_height;
inner_w = outer_w - 2 * wall;
inner_h = outer_h - 2 * wall;

lid_screw_x = outer_w / 2 - lid_screw_margin;
lid_screw_y = outer_h / 2 - lid_screw_margin;

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
                        d = board_screw_pilot_d,
                        h = display_front_to_board_mount - board_screw_pilot_front_skin + 0.2
                    );
            }
        }
    }
}

module lid_bosses() {
    at_corners(lid_screw_x, lid_screw_y) {
        difference() {
            cylinder(d = lid_boss_d, h = main_depth);
            translate([0, 0, front_wall])
                cylinder(d = lid_pilot_d, h = main_depth);
        }
    }
}

module usb_side_legs() {
    leg_y = outer_h / 2 + leg_len / 2;

    // Front pair, flush with the front plane and aligned to the case corners.
    for (sx = [-1, 1]) {
        translate([sx * (outer_w / 2 - leg_w / 2), leg_y, 0])
            linear_extrude(height = leg_depth)
                square([leg_w, leg_len], center = true);
    }

    // Rear pair, aligned with the lid side and inset from the long edges.
    for (sx = [-1, 1]) {
        translate([sx * (outer_w / 2 - rear_leg_inset - leg_w / 2), leg_y, main_depth - leg_depth])
            linear_extrude(height = leg_depth)
                square([leg_w, leg_len], center = true);
    }
}

module front_leg_corner_fill() {
    for (sx = [-1, 1]) {
        translate([
            sx * (outer_w / 2 - corner_radius / 2),
            outer_h / 2 - corner_radius / 2,
            leg_depth / 2
        ])
            cube([corner_radius, corner_radius, leg_depth], center = true);
    }
}

module side_arms() {
    arm_y = -outer_h / 2 + outer_h * arm_from_top;

    for (sx = [-1, 1]) {
        translate([
            sx * (outer_w / 2 + arm_len / 2),
            arm_y,
            arm_depth / 2
        ])
            cube([arm_len, arm_w, arm_depth], center = true);
    }
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
    translate([-outer_w / 2, usb_cutout_y, usb_cutout_z])
        cube([wall + 4.0, usb_cutout_w, usb_cutout_h], center = true);
}

module usb_channel_cutout(z_min, z_max) {
    board_left_x = board_center_x - board_w / 2;
    x_min = -outer_w / 2 - usb_channel_outer_overlap;
    x_max = board_left_x + usb_channel_board_overlap;

    translate([
        (x_min + x_max) / 2,
        usb_cutout_y,
        (z_min + z_max) / 2
    ])
        cube([
            x_max - x_min,
            usb_cutout_w + usb_channel_clearance,
            z_max - z_min
        ], center = true);
}

module usb_channel_open_side_cutout(z_min, z_max) {
    board_left_x = board_center_x - board_w / 2;
    x_min = -outer_w / 2 - usb_channel_outer_overlap;
    x_max = board_left_x - usb_channel_wall_thickness;

    translate([
        (x_min + x_max) / 2,
        usb_cutout_y,
        (z_min + z_max) / 2
    ])
        cube([
            x_max - x_min,
            usb_cutout_w + usb_channel_clearance,
            z_max - z_min
        ], center = true);
}

module usb_channel_walls() {
    board_left_x = board_center_x - board_w / 2;
    x_min = -outer_w / 2 + wall - 0.2;
    x_max = board_left_x;
    channel_w = usb_cutout_w + usb_channel_clearance;

    for (sy = [-1, 1]) {
        translate([
            (x_min + x_max) / 2,
            usb_cutout_y + sy * (channel_w / 2 + usb_channel_wall_thickness / 2),
            (display_front_to_board_mount + main_depth) / 2
        ])
            cube([
                x_max - x_min,
                usb_channel_wall_thickness,
                main_depth - display_front_to_board_mount
            ], center = true);
    }
}

module usb_channel_inner_wall() {
    board_left_x = board_center_x - board_w / 2;
    wall_x = board_left_x - usb_channel_wall_thickness / 2;
    channel_w = usb_cutout_w + usb_channel_clearance;

    difference() {
        translate([
            wall_x,
            usb_cutout_y,
            (display_front_to_board_mount + main_depth) / 2
        ])
            cube([
                usb_channel_wall_thickness,
                channel_w + 2 * usb_channel_wall_thickness,
                main_depth - display_front_to_board_mount
            ], center = true);

        translate([wall_x, usb_cutout_y, usb_cutout_z])
            cube([
                usb_channel_wall_thickness + 0.4,
                usb_cutout_w,
                usb_cutout_h
            ], center = true);
    }
}

module lid_usb_hole() {
    board_left_x = board_center_x - board_w / 2;
    x_max = board_left_x - usb_channel_wall_thickness;

    translate([
        x_max - usb_lid_hole_d / 2,
        usb_cutout_y,
        -lid_lip_depth - 0.1
    ])
        cylinder(d = usb_lid_hole_d, h = lid_thickness + lid_lip_depth + 0.2);
}

module lid_usb_edge_opening() {
    board_left_x = board_center_x - board_w / 2;
    x_max = board_left_x - usb_channel_wall_thickness;
    hole_center_x = x_max - usb_lid_hole_d / 2;
    x_min = -outer_w / 2 - 0.2;

    translate([
        (x_min + hole_center_x) / 2,
        usb_cutout_y,
        (lid_thickness - lid_lip_depth) / 2
    ])
        cube([
            hole_center_x - x_min,
            usb_lid_edge_opening,
            lid_thickness + lid_lip_depth + 0.2
        ], center = true);
}

module long_edge_clearance() {
    pocket_depth = wall - long_edge_remaining_wall + long_edge_overlap;
    pocket_y = inner_h / 2 - long_edge_overlap + pocket_depth / 2;

    for (sy = [-1, 1]) {
        translate([0, sy * pocket_y, long_edge_clearance_z])
            cube([long_edge_clearance_len, pocket_depth, long_edge_clearance_h], center = true);
    }
}

module main_case() {
    difference() {
        union() {
            main_shell();
            board_posts();
            lid_bosses();
            usb_side_legs();
            front_leg_corner_fill();
            side_arms();
            usb_channel_walls();
            usb_channel_inner_wall();
        }

        screen_cutout();
        usb_cutout();
        usb_channel_open_side_cutout(display_front_to_board_mount, main_depth + 0.2);
        long_edge_clearance();
    }
}

module lid_raw() {
    union() {
        difference() {
            union() {
                rounded_box(outer_w, outer_h, lid_thickness, corner_radius);

                translate([0, 0, -lid_lip_depth])
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
            }

            at_corners(lid_screw_x, lid_screw_y) {
                translate([0, 0, -lid_lip_depth - 0.1])
                    cylinder(d = lid_boss_d + 0.8, h = lid_lip_depth + 0.2);

                translate([0, 0, -lid_lip_depth - 0.1])
                    cylinder(d = lid_screw_d, h = lid_thickness + lid_lip_depth + 0.2);

                translate([0, 0, lid_thickness - counterbore_depth])
                    cylinder(d = counterbore_d, h = counterbore_depth + 0.1);
            }

            lid_usb_hole();
            lid_usb_edge_opening();
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
} else if (part == "lid") {
    lid_for_print();
} else {
    assembly();
}
