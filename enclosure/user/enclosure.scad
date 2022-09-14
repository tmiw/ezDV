// Makes the preview renders look better.
$fa = 1;
$fs = 0.4;

/*
// Outside case dimensions
width = 72.64;
height = 87.38;
caseThickness = 1;
totalHeight = 18;

// Locations of mounting hole centers
mountingHoleCenters = [
    [ 4.32, 4.32 ],
    [ 68.58, 4.57 ],
    [ 4.32, 83.31 ],
    [ 68.58, 83.31 ]
];

// M3 flat screw
mountingHoleDiameter = 3;
mountngHoleHeadDiameter = 5.45;

difference() {
    // Overall area
    cube([width + caseThickness, height + caseThickness, totalHeight - 1]);
    
    // Subtract inner case volume
    translate([caseThickness, caseThickness, caseThickness]) cube([width - caseThickness*2, height - caseThickness*2, totalHeight - 1]);
    
    // Subtract mounting holes
    for (holeLocation = mountingHoleCenters) {
        translate([holeLocation.x + caseThickness, holeLocation.y + caseThickness, -0.1]) cylinder(caseThickness + 0.2, d1 = mountngHoleHeadDiameter, d2 = mountingHoleDiameter);
    }
}
*/

include <../library/YAPP_Box/library/YAPPgenerator_v14.scad>

printBaseShell      = false;
printLidShell       = true;

// Edit these parameters for your own board dimensions
wallThickness       = 2.0;
basePlaneThickness  = 2.0;
lidPlaneThickness   = 2.0;

// Total height of box = basePlaneThickness + lidPlaneThickness 
//                     + baseWallHeight + lidWallHeight

baseWallHeight      = 13;
lidWallHeight       = 7.5;

pcbLength           = 69.85;
pcbWidth            = 64.70;
pcbThickness        = 1.6;
                            
// padding between pcb and inside wall
paddingFront        = 1;
paddingBack         = 7.62;
paddingRight        = 1;
paddingLeft         = 1;

// ridge where Base- and Lid- off the box can overlap
// Make sure this isn't less than lidWallHeight
ridgeHeight         = 3;
ridgeSlack          = 0.2;
roundRadius         = 2.0;  // don't make this to big..

//-- How much the PCB needs to be raised from the base
//-- to leave room for solderings and whatnot
standoffHeight      = 10.0;
pinDiameter         = 2.5;
pinHoleSlack        = 0.5;
standoffDiameter    = 5;

pcbStands = [
    [ 5.08, 5.08, yappBoth, yappPin ],
    [ 15.24, 60.96, yappBoth, yappPin ],
    [ 66.04, 5.08, yappBoth, yappPin ],
    [ 66.04, 60.96, yappBoth, yappPin ]
];
     
cutoutsFront = [
    [16.51, 3.5, 8, 8, 0, yappCircle, yappCenter ],
    [45.72, 3.5, 8, 8, 0, yappCircle, yappCenter ],
    [22.75, -2.5, 12.5, 7, 0, yappRectangle ],
    [34.56, -2.5, 6, 1, 0, yappRectangle ]
];

snapJoins = [
    [(shellLength/2)-2.5, 5, yappLeft, yappRight],
    [(shellWidth/2)-2.5, 5, yappFront, yappBack]
];

cutoutsLid = [
    // Buttons
    [3, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    [11.89, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    [20.78, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    [29.67, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    
    // Status LEDs
    [2.54, 55.88, 2, 2, 0, yappCircle],
    [5.08, 55.88, 2, 2, 0, yappCircle],
    [7.62, 55.88, 2, 2, 0, yappCircle],
    [10.16, 55.88, 2, 2, 0, yappCircle],
];

module addButton(x, y)
{
    translate([pcbX + y, pcbY + x, -4])
    {
        union()
        {
            translate([-2.5, -2.5, 0]) difference()
            {
                color("red") cube([5,5,2]);
                //color("black") translate([0.25,0.25,0.25]) cube([4.5,4.5,/*3.25,3.25,*/1]);
                translate([0.75,0.75,-1]) color("blue") cube([3.5,3.5,13]);
                //translate([0,0,4]) cube([3.5,3.5,1]);
                //translate([0.3,0.3,2]) cube([3,3,2]);
            };
            translate([0,0,-lidWallHeight+pcbThickness+4.5]) cube([2,2,(baseWallHeight-standoffHeight)+lidWallHeight], center=true);
            translate([0,0,3.5]) color("green") cube([3.3,3.3,1], center=true);
            translate([0,0,-lidWallHeight+pcbThickness+1]) cube([3,3,2], center=true);
            translate([0,0,-lidWallHeight+pcbThickness-1]) cube([5.5,5.5,2], center=true);
        }
    }
}

module lidHookInside()
{
    addButton(14.63, 3);
    addButton(14.63, 11.89);
    addButton(14.63, 20.78);
    addButton(14.63, 29.67);
}

YAPPgenerate();
