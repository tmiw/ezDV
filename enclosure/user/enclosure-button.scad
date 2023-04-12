// Makes the preview renders look better.
$fa = 1;
$fs = 0.4;

rotate([0, 180, 0]) {
    translate([0, 0, -3]) {
        difference() {
            cylinder(h = 5, r = 2.1, center = true);
            translate([0, 0, -4]) cylinder(h = 10, r = 1.5, center = true);
        }
    }

    translate([0, 0, -1]) {
        cylinder(h = 2, r = 3, center = true);
    }
}