// Makes the preview renders look better.
$fa = 1;
$fs = 0.4;

// BEGIN configuration

// Whether to print a taller enclosure to fit e.g. pin headers.
// Note: this weirdly has to be above the include line for some reason.
printTallerEnclosure = false;

include <../library/YAPP_Box/library/YAPPgenerator_v14.scad>

// Which sides to print. Base = bottom, lid = top.
// One or the other can be disabled to print on smaller print beds.
printBaseShell      = false;
printLidShell       = true;

// END configuration

wallThickness       = 2.0;
basePlaneThickness  = 2.0;
lidPlaneThickness   = 1.0;

// Total height of box = basePlaneThickness + lidPlaneThickness 
//                     + baseWallHeight + lidWallHeight

baseWallHeight      = 14.5;
lidWallHeight = (printTallerEnclosure) ? 14.5 : 3.5;
echo(lidWallHeight);
echo(printTallerEnclosure);

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
standoffHeight      = 11.0;
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
    [14.97, 2.5, 7.5, 7, 0, yappCircle, yappCenter ],
    [48.26, 2.5, 7.5, 7, 0, yappCircle, yappCenter ],
    [21.75, -1.5, 12.5, 7, 0, yappRectangle ],
    [37.56, -0.5, 2, 1, 0, yappRectangle ]
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
    
    [2.87, 14.62, 5.2, 5.2, 0, yappCircle, yappCenter],
    [12.06, 14.62, 5.7, 5.2, 0, yappCircle, yappCenter],
    [20.65, 14.62, 5.2, 5.2, 0, yappCircle, yappCenter],
    [29.54, 14.62, 5.2, 5.2, 0, yappCircle, yappCenter],
    
    // Status LEDs
    [2.54, 55.88, 2, 2, 0, yappCircle],
    [5.08, 55.88, 2, 2, 0, yappCircle],
    [7.62, 55.88, 2, 2, 0, yappCircle],
    [10.16, 55.88, 2, 2, 0, yappCircle],
];

YAPPgenerate();
