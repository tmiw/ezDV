// Makes the preview renders look better.
$fa = 1;
$fs = 0.4;

rotate([180,0,0]) {
    union() {
        difference()
        {
            cube([100, 10, 2], center = true);
            translate([33, 0, 0]) cylinder(h = 2.5, r = 3.5, center = true);
            translate([24.5, 0, 0]) cylinder(h = 2.5, r = 3.5, center = true);
            translate([16, 0, 0]) cylinder(h = 2.5, r = 3.5, center = true);
            translate([7.5, 0, 0]) cylinder(h = 2.5, r = 3.5, center = true);
        };

        buttonBody(33, 0);
        buttonBody(24.5, 0);
        buttonBody(16, 0);
        buttonBody(7.5, 0);
    }
}
    
module buttonBody(x, y) {
    translate([x, y, -0.2]) {
            rotate_extrude(angle = 180, convexity = 2) { 
                translate([2.5,0,0]) { circle(.4); };
            };
           
            translate([1.7, 0, 0]) rotate(a=[-90,0,-90]) linear_extrude(height = 3.3, center = false, convexity = 10, twist = 0) { circle(0.4); };
            
            rotate(a=[0,180,0]) rotate_extrude(angle = -180, convexity = 2) { 
                translate([2.5,0,0]) { circle(.4); };
            };
            
            translate([-4.7, 0, 0]) rotate(a=[-90,0,-90]) linear_extrude(height = 3, center = false, convexity = 10, twist = 0) { circle(0.4); };
            
            translate([0, -2.2, 0]) rotate(a=[-90,0,0]) linear_extrude(height = 5, center = false, convexity = 10, twist = 0) { circle(0.4); };
            
            translate([0, 0, -4]) cylinder(h = 11, r = 1.5, center = true);
        } 
};