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

include <./library/YAPP_Box/library/YAPPgenerator_v14.scad>

printBaseShell      = true;
printLidShell       = false;

// Edit these parameters for your own board dimensions
wallThickness       = 2.0;
basePlaneThickness  = 2.0;
lidPlaneThickness   = 2.0;

// Total height of box = basePlaneThickness + lidPlaneThickness 
//                     + baseWallHeight + lidWallHeight

baseWallHeight      = 16;
lidWallHeight       = 12;

pcbLength           = 87.38;
pcbWidth            = 72.64;
pcbThickness        = 1.6;
                            
// padding between pcb and inside wall
paddingFront        = 2;
paddingBack         = 5;
paddingRight        = 2;
paddingLeft         = 2;

// ridge where Base- and Lid- off the box can overlap
// Make sure this isn't less than lidWallHeight
ridgeHeight         = 3;
ridgeSlack          = 0.2;
roundRadius         = 2.0;  // don't make this to big..

//-- How much the PCB needs to be raised from the base
//-- to leave room for solderings and whatnot
standoffHeight      = 5.0;
pinDiameter         = 2.5;
pinHoleSlack        = 0.5;
standoffDiameter    = 5;

pcbStands = [
    [ 4.32, 4.32, yappBoth, yappPin ],
    [ 4.57, 68.58, yappBoth, yappPin ],
    [ 83.31, 4.32, yappBoth, yappPin ],
    [ 83.31, 68.58, yappBoth, yappPin ]
];
     
cutoutsFront = [
    [30.99, 2.4, 8, 8, 0, yappCircle, yappCenter ],
    [48.77, 2.4, 8, 8, 0, yappCircle, yappCenter ],
    [22.15, 10, 28.21, 8.59, 0, yappRectangle ]
];
YAPPgenerate();