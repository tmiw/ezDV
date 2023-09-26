// Makes the preview renders look better.
$fa = 1;
$fs = 0.4;

// Whether to print a taller buttons (typically used when printing the taller enclosure).
printTallerButtons = false;

rotate([0, 180, 0]) {
    translate([0, 0, -3]) {
        if (printTallerButtons) {
            difference() {
                cylinder(h = 15, r = 2.1, center = true);
                translate([0, 0, -12]) cylinder(h = 10, r = 1.5, center = true);
            }
        } else {
            difference() {
                cylinder(h = 5, r = 2.1, center = true);
                translate([0, 0, -4]) cylinder(h = 10, r = 1.5, center = true);
            }
        }
    }

    translate([0, 0, printTallerButtons ? 3.5 : -1]) {
        cylinder(h = 2, r = 3, center = true);
    }
}