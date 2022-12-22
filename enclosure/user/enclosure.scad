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
lidPlaneThickness   = 1.0;

// Total height of box = basePlaneThickness + lidPlaneThickness 
//                     + baseWallHeight + lidWallHeight

baseWallHeight      = 13.5;
lidWallHeight       = 3.5;

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
    [14.97, 2.5, 7, 7, 0, yappCircle, yappCenter ],
    [48.26, 2.5, 7, 7, 0, yappCircle, yappCenter ],
    [21.75, -1.5, 12.5, 7, 0, yappRectangle ],
    //[34.56, -2.5, 6, 1, 0, yappRectangle ]
];

snapJoins = [
    [(shellLength/2)-2.5, 5, yappLeft, yappRight],
    [(shellWidth/2)-2.5, 5, yappFront, yappBack]
];

cutoutsLid = [
    // Buttons
    /*[3, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    [11.89, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    [20.78, 14.63, 4, 4, 0, yappRectangle, yappCenter],
    [29.67, 14.63, 4, 4, 0, yappRectangle, yappCenter],*/
    
    [3.17, 14.62, 3, 3, 0, yappCircle, yappCenter],
    [12.06, 14.62, 3, 3, 0, yappCircle, yappCenter],
    [20.95, 14.62, 3, 3, 0, yappCircle, yappCenter],
    [29.84, 14.62, 3, 3, 0, yappCircle, yappCenter],
    
    // Status LEDs
    [2.54, 55.88, 2, 2, 0, yappCircle],
    [5.08, 55.88, 2, 2, 0, yappCircle],
    [7.62, 55.88, 2, 2, 0, yappCircle],
    [10.16, 55.88, 2, 2, 0, yappCircle],
];

YAPPgenerate();
