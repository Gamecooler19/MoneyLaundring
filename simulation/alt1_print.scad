// =====================================================================
//  ALT 1 - COMPLETE 3D PRINT, no glue, no laser (v8)  - screws only
//  Rack A : slides through a FIXED guide + pinion (motor A fixed on your base).
//  Junction: ONE rigid part that (1) clamps the end of rack A with 2 screws,
//            (2) carries pinion B / motor B, (3) guides rack B.
//            => end of rack A, motor-B mount and rack B are all tied together.
//  Rack B : slides through the junction.  Tool holder clamps the free end of rack B.
//  Motor housings / IR sensors intentionally left out (shaft holes are there).
//  Parts: rack x2, guide x1 (A), junction x1 (B), pinion x2, tool_holder x1
//  part = "assembly" | "rack" | "guide" | "junction" | "pinion" | "tool_holder"
// =====================================================================
part = "assembly";
$fn = 48;
m=1.5; pa=20; p=PI*m; add=m; ded=1.25*m; tooth_h=add+ded;
zp=20; rp=m*zp/2;
rack_w=12; rack_d=12; wall=1.2;
teeth_n=50; Lt=teeth_n*p;                 // 235.6 mm teeth
L=250; ez=L-Lt;                           // both axes use a 250 mm rack; 14.4 mm solid end zone
gap=0.3; plate_t=3;
bo_shaft_d=5.5; bo_flat=3.7; bo_w=19;              // BO motor double-D shaft (edit to match yours)
y_p=-(ded+rp);                            // pinion centre (rack root at y=0)
ZB=15.6;                                  // rack-B layer height (rack A top 12 + junction plate 3 + gaps)
n_home_A=44; n_home_B=45; n_end=2;        // home and end positions, measured in rack-tooth pitches
travel_A=(n_home_A-n_end)*p;              // 197.920 mm: 42 pitches
travel_B=(n_home_B-n_end)*p;              // 202.633 mm: 43 pitches
tA=100; tB=100;                           // preview position in mm, clamped to the CAD travel below

function inv_r(a) = tan(a) - a*PI/180;
function halfang(z, r) = let(rpz=m*z/2, rb=rpz*cos(pa), a=acos(min(1, rb/max(r,rb))))
                         90/z + (inv_r(pa)-inv_r(a))*180/PI;
module gear2d(zt){
  rpz=m*zt/2; r0=rpz-ded; rt=rpz+add; N=8; h0=halfang(zt,r0);
  circle(r=r0, $fn=96);
  for(i=[0:zt-1]) rotate(i*360/zt)
    polygon(concat(
      [[(r0-0.6)*cos(-h0),(r0-0.6)*sin(-h0)]],
      [for(k=[0:N]) let(r=r0+(rt-r0)*k/N, h=halfang(zt,r)) [r*cos(-h), r*sin(-h)]],
      [for(k=[N:-1:0]) let(r=r0+(rt-r0)*k/N, h=halfang(zt,r)) [r*cos(h), r*sin(h)]],
      [[(r0-0.6)*cos(h0),(r0-0.6)*sin(h0)]]));
}
module pinion(){                          // 5.2 mm wide, double-D bore, M3 set screw
  th=5.2;
  difference(){
    linear_extrude(th) gear2d(zp);
    translate([0,0,-1]) linear_extrude(th+2) rotate(180/zp)
      intersection(){ circle(d=bo_shaft_d); square([bo_flat,10],center=true); }
    translate([0,0,th/2]) rotate([0,0,180/zp]) rotate([0,90,0]) cylinder(d=2.8,h=rp+3);
  }
}

// ---------------- shared 250 mm rack (both axes) ----------------
module tooth2d(){
  t=tan(pa); hr=p/4+ded*t; ht=p/4-add*t;
  polygon([[-hr,0.3],[hr,0.3],[hr,0],[ht,-tooth_h],[-ht,-tooth_h],[-hr,0]]);
}
module rack(){        // tail x=0 (closed), tip x=250. Body y 0..12, teeth toward -y.
  difference(){
    union(){
      difference(){
        cube([L,rack_d,rack_w]);
        translate([wall,wall,wall]) cube([Lt-2*wall,rack_d-2*wall,rack_w-2*wall]);
      }
      linear_extrude(rack_w) for(i=[0:teeth_n-1]) translate([(i+0.5)*p,0]) tooth2d();
    }
    for(xh=[L-4,L-10])                    // 2 cross screws through the solid end zone (junction / tool holder)
      translate([xh,-1,rack_w/2]) rotate([-90,0,0]) cylinder(d=3.2,h=rack_d+2);
  }
}

// ---------------- C-channel guide (shared core) ----------------
module guide_core(){  // rack passes along x, teeth side (-y) open for the pinion, back wall at y>12
  xw=20; ymin=-46; ymax=rack_d+gap+plate_t;
  difference(){
    union(){
      translate([-xw,ymin,-gap-plate_t]) cube([2*xw,ymax-ymin,plate_t]);                       // bottom plate
      translate([-xw,ymin,rack_w+gap])   cube([2*xw,ymax-ymin,plate_t]);                       // top plate (motor side)
      translate([-xw,rack_d+gap,-gap-plate_t]) cube([2*xw,plate_t,rack_w+2*gap+2*plate_t]);    // back wall
    }
    translate([0,0,rack_w+gap-1]) linear_extrude(plate_t+2) hull() for(d=[-2,2]) translate([0,y_p+d]) circle(d=8);   // shaft slot +-2 mm
    for(sx=[-1,1], yy=[-37.5,-43]) translate([sx*(bo_w/2+1.8)-0.9,yy-1.75,rack_w+gap-1]) cube([1.8,3.5,plate_t+2]);   // zip-tie slots
  }
}
module guide(){       // fixed guide for rack A: bolts to your base
  difference(){
    guide_core();
    for(sx=[-14,14], yy=[-25,8]){                                                               // 4x M4 countersunk
      translate([sx,yy,-5]) cylinder(d=4.3,h=6);
      translate([sx,yy,-gap-2.2]) cylinder(d1=4.3,d2=8.6,h=2.2);
    }
  }
}
module junction(){    // guide for rack B + clamp on the end of rack A, one rigid part
  difference(){
    union(){
      guide_core();
      for(s=[-1,1]) translate([s*7.8-1.5,-1.5,-gap-plate_t-12.6]) cube([3,14.7,12.6+0.1]);   // clamp walls straddling rack A's end
    }
    for(ly=[2.8,8.8]){                                                                          // cross screws M3x25
      translate([-12,ly,-gap-plate_t-6.3]) rotate([0,90,0]) cylinder(d=3.2,h=24);
      translate([9.3-2.5,ly,-gap-plate_t-6.3]) rotate([0,90,0]) cylinder(d=6.4,h=2.6,$fn=6);    // nut trap
    }
  }
}

// ---------------- box holder: clamps the free end of rack B, carries the dart-board box ----------------
bx=50; by=60; bw=1.6;                     // box (open cup, face for the board on top)
module tool_holder(){
  difference(){
    union(){
      translate([Lt,-3.3,rack_w+gap]) cube([L+3.5-Lt,rack_d+6.6,plate_t]);                          // ceiling over the end zone
      for(yy=[-3.3,rack_d+gap]) translate([Lt,yy,-gap]) cube([L+3.5-Lt,3,rack_w+2*gap+0.1]);          // clamp walls
      difference(){                                                                                    // cup: closed top = board face
        translate([L+3.3,6-by/2,-gap-plate_t]) cube([bx,by,rack_w+2*gap+2*plate_t]);
        translate([L+3.3+bw,6-by/2+bw,-gap-plate_t-1]) cube([bx-2*bw,by-2*bw,rack_w+2*gap+2*plate_t-2.4+1]);
      }
    }
    for(xh=[L-4,L-10]){
      translate([xh,-6,rack_w/2]) rotate([-90,0,0]) cylinder(d=3.2,h=26);
      translate([xh,rack_d+gap+3-2.5,rack_w/2]) rotate([-90,0,0]) cylinder(d=6.4,h=2.6,$fn=6);
    }
  }
}

// ---------------- assembly ----------------
module assembly(){
  posA=min(max(tA,0),travel_A); posB=min(max(tB,0),travel_B);
  tailA=-p*(n_home_A-round(posA/p)); tipA=tailA+L;
  tailB=-p*(n_home_B-round(posB/p));
  color("orange") guide();  color("gold") translate([0,y_p,7.0]) pinion();       // fixed: motor A side
  translate([tailA,0,0]) color("lightgray") rack();                                // rack A
  translate([tipA-1.2,6,ZB]) rotate([0,0,90]) {                                    // rides on the end of rack A
    color("orange") junction();  color("gold") translate([0,y_p,7.0]) pinion();    // motor B side
    translate([tailB,0,0]) { color("lightblue") rack(); color("tomato") tool_holder(); }
  }
}

if(part=="assembly")         assembly();
else if(part=="rack")        translate([0,0,rack_d]) rotate([-90,0,0]) rack();                       // teeth up
else if(part=="guide")       translate([0,0,rack_d+gap+plate_t]) rotate([-90,0,0]) guide();           // on its back wall
else if(part=="junction")    translate([0,0,rack_d+gap+plate_t]) rotate([-90,0,0]) junction();        // on its back wall
else if(part=="pinion")      pinion();
else if(part=="tool_holder") translate([0,0,rack_w+gap+plate_t]) rotate([180,0,0]) tool_holder();    // board face down on the bed
