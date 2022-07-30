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

printBaseShell      = false;
printLidShell       = true;

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

snapJoins = [
    [(shellLength/2)-2.5, 5, yappLeft, yappRight],
    [(shellWidth/2)-2.5, 5, yappFront, yappBack]
];

cutoutsLid = [
    // Buttons
    [10.67, 14.48, 4, 4, 0, yappRectangle, yappCenter],
    [19.56, 14.48, 4, 4, 0, yappRectangle, yappCenter],
    [28.45, 14.48, 4, 4, 0, yappRectangle, yappCenter],
    [37.34, 14.48, 4, 4, 0, yappRectangle, yappCenter],
    
    // Status LEDs
    [9.65, 66.04, 2, 2, 0, yappCircle],
    [13.46, 66.04, 2, 2, 0, yappCircle],
    [17.27, 66.04, 2, 2, 0, yappCircle],
    [21.08, 66.04, 2, 2, 0, yappCircle],
];

module addButton(x, y)
{
    translate([pcbX + y, pcbY + x, -4])
    {
        union()
        {
            translate([-2.5, -2.5, -2]) difference()
            {
                color("red") cube([5,5,5]);
                color("black") translate([0.25,0.25,3.25]) cube([4.5,4.5,/*3.25,3.25,*/3]);
                translate([1.25,1.25,-1]) color("blue") cube([2.5,2.5,13]);
                //translate([0,0,4]) cube([3.5,3.5,1]);
                //translate([0.3,0.3,2]) cube([3,3,2]);
            };
            translate([0,0,-lidWallHeight+pcbThickness+2.4]) cube([2,2,(baseWallHeight-standoffHeight)+lidWallHeight+0.9], center=true);
            translate([0,0,3]) color("green") cube([3,3,2], center=true);
            translate([0,0,-lidWallHeight+pcbThickness-7]) cube([3,3,2], center=true);
            translate([0,0,-lidWallHeight+pcbThickness-9]) cube([5.5,5.5,2], center=true);
        }
    }
}

module lidHookInside()
{
    addButton(14.48, 10.67);
    addButton(14.48, 19.56);
    addButton(14.48, 28.45);
    addButton(14.48, 37.34);
}

YAPPgenerate();