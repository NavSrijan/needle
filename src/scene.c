/*
 * A late-night listening room, drawn entirely with Cairo.
 * Drop-in replacement for scene.c: the public function is unchanged.
 * All shapes use a 1200 x 800 design space, then scale to the supplied size.
 * The record/platter centre remains at (0.643 * w, 0.566 * h) so a player
 * that composites its animated vinyl artwork can keep using that anchor.
 * No images, extra libraries, randomness or frame-dependent state required.
 */
#include <cairo.h>
#include <math.h>
#include <stdint.h>
#include "needle.h"

#define SCENE_PI 3.14159265358979323846
#define SCENE_RGB(x) (((x)>>16&255)/255.0), (((x)>>8&255)/255.0), (((x)&255)/255.0)

static void ink(cairo_t *c, uint32_t rgb, double a) {
    cairo_set_source_rgba(c, SCENE_RGB(rgb), a);
}
static void box(cairo_t *c, double x, double y, double w, double h,
                uint32_t rgb, double a) {
    cairo_rectangle(c,x,y,w,h); ink(c,rgb,a); cairo_fill(c);
}
static void rounded_path(cairo_t *c,double x,double y,double w,double h,double r) {
    r=fmax(0,fmin(r,fmin(w,h)*.5));
    cairo_new_sub_path(c);
    cairo_arc(c,x+w-r,y+r,r,-SCENE_PI/2,0);
    cairo_arc(c,x+w-r,y+h-r,r,0,SCENE_PI/2);
    cairo_arc(c,x+r,y+h-r,r,SCENE_PI/2,SCENE_PI);
    cairo_arc(c,x+r,y+r,r,SCENE_PI,3*SCENE_PI/2);
    cairo_close_path(c);
}
static void round_box(cairo_t *c,double x,double y,double w,double h,double r,
                      uint32_t rgb,double a) {
    rounded_path(c,x,y,w,h,r); ink(c,rgb,a); cairo_fill(c);
}
static void oval(cairo_t *c,double x,double y,double rx,double ry,
                 uint32_t rgb,double a) {
    if(rx<=0||ry<=0) return;
    cairo_save(c); cairo_translate(c,x,y); cairo_scale(c,rx,ry);
    cairo_arc(c,0,0,1,0,2*SCENE_PI); ink(c,rgb,a); cairo_fill(c);
    cairo_restore(c);
}
static void oval_ring(cairo_t *c,double x,double y,double rx,double ry,
                      double width,uint32_t rgb,double a) {
    if(rx<=0||ry<=0) return;
    cairo_save(c); cairo_translate(c,x,y); cairo_scale(c,rx,ry);
    cairo_arc(c,0,0,1,0,2*SCENE_PI); ink(c,rgb,a);
    cairo_set_line_width(c,width); cairo_stroke(c);
    cairo_restore(c);
}
static void segment(cairo_t *c,double x,double y,double xx,double yy,
                    double width,uint32_t rgb,double a) {
    cairo_move_to(c,x,y); cairo_line_to(c,xx,yy);
    ink(c,rgb,a); cairo_set_line_width(c,width); cairo_stroke(c);
}
static void polygon(cairo_t *c,const double *xy,int n,uint32_t rgb,double a) {
    cairo_move_to(c,xy[0],xy[1]);
    for(int i=1;i<n;i++) cairo_line_to(c,xy[i*2],xy[i*2+1]);
    cairo_close_path(c); ink(c,rgb,a); cairo_fill(c);
}
static void linear_box(cairo_t *c,double x,double y,double w,double h,
                       double x0,double y0,double x1,double y1,
                       uint32_t top,uint32_t bottom) {
    cairo_pattern_t *p=cairo_pattern_create_linear(x0,y0,x1,y1);
    cairo_pattern_add_color_stop_rgb(p,0,SCENE_RGB(top));
    cairo_pattern_add_color_stop_rgb(p,1,SCENE_RGB(bottom));
    cairo_rectangle(c,x,y,w,h); cairo_set_source(c,p); cairo_fill(c);
    cairo_pattern_destroy(p);
}
static void radial_glow(cairo_t *c,double x,double y,double radius,
                        uint32_t rgb,double opacity) {
    cairo_pattern_t *p=cairo_pattern_create_radial(x,y,0,x,y,radius);
    cairo_pattern_add_color_stop_rgba(p,0,SCENE_RGB(rgb),opacity);
    cairo_pattern_add_color_stop_rgba(p,.4,SCENE_RGB(rgb),opacity*.32);
    cairo_pattern_add_color_stop_rgba(p,1,SCENE_RGB(rgb),0);
    cairo_rectangle(c,x-radius,y-radius,2*radius,2*radius);
    cairo_set_source(c,p); cairo_fill(c); cairo_pattern_destroy(p);
}
static uint32_t scramble(uint32_t n) {
    n^=n>>16; n*=0x7feb352du; n^=n>>15; n*=0x846ca68bu;
    return n^(n>>16);
}

static void draw_wall(cairo_t *c) {
    linear_box(c,0,0,1200,540,0,0,1100,650,0x1a2328,0x4a3830);
    /* More restrained, slightly more realistic wall tone and light falloff. */
    radial_glow(c,205,210,430,0xf0a365,.15);
    radial_glow(c,1100,315,385,0xf09a47,.17);
    linear_box(c,0,412,1200,100,0,412,0,510,0x343636,0x2b2c2b);
    box(c,0,415,1200,2,0xb18d68,.24);
    box(c,0,511,1200,7,0x231d1d,1);
    box(c,0,518,1200,2,0x8b6547,.30);
    for(int i=0;i<140;i++) {
        uint32_t k=scramble((uint32_t)i+77);
        double x=(double)(k%1200), y=(double)((k>>11)%405);
        segment(c,x,y,x+7+(k>>22)%24,y+1.2,0.7,0xf3dcc0,.015);
    }
    /* Slight architectural shadow rhythm behind furniture. */
    for(int i=0;i<13;i++) {
        double x=23+i*96;
        segment(c,x,422,x,508,.8,0x0d1e24,.12);
    }
    /* Soft shadows from the gallery and shelf add depth. */
    box(c,350,270,640,10,0x101417,.18);
    box(c,332,490,720,14,0x101417,.15);
}

static void draw_window(cairo_t *c) {
    const double x=25,y=19,w=277,h=386;
    round_box(c,x+10,y+12,w,h,17,0x0e1417,.56);
    round_box(c,x-7,y-7,w+14,h+14,18,0x1d1919,1);
    round_box(c,x-2,y-3,w+4,h+7,16,0x79533b,1);
    round_box(c,x+8,y+8,w-16,h-15,10,0x121f2a,1);
    cairo_save(c);
    rounded_path(c,x+12,y+12,w-24,h-27,8); cairo_clip(c);
    cairo_pattern_t *sky=cairo_pattern_create_linear(0,y,0,y+h);
    cairo_pattern_add_color_stop_rgb(sky,0,SCENE_RGB(0x24344f));
    cairo_pattern_add_color_stop_rgb(sky,.38,SCENE_RGB(0x6a5970));
    cairo_pattern_add_color_stop_rgb(sky,.72,SCENE_RGB(0xcc8b79));
    cairo_pattern_add_color_stop_rgb(sky,1,SCENE_RGB(0xf1b56e));
    cairo_rectangle(c,x+12,y+12,w-24,h-27);
    cairo_set_source(c,sky); cairo_fill(c); cairo_pattern_destroy(sky);
    radial_glow(c,204,254,152,0xffd38f,.56);
    oval(c,216,255,28,28,0xffdf9f,.72);
    for(int i=0;i<20;i++) {
        uint32_t k=scramble(1601+(uint32_t)i);
        double px=43+k%231, py=37+(k>>11)%140;
        oval(c,px,py,((k>>24)%3==0)?1.1:.6,.6,0xffead7,.23);
    }
    const double mountains1[]={36,325,84,291,120,315,169,275,214,304,291,269,291,389,36,389};
    polygon(c,mountains1,8,0x746f78,.56);
    const double mountains2[]={28,356,76,323,122,341,174,310,225,342,298,309,298,395,28,395};
    polygon(c,mountains2,8,0x49515d,.88);
    for(int i=0;i<15;i++) {
        double bx=32+i*19, bh=20+(scramble(i+9)%39);
        box(c,bx,389-bh,17,bh,0x252f39,.68);
        if(i%3==0) box(c,bx+5,396-bh,3,3,0xffc07b,.36);
    }
    for(int i=0;i<8;i++) {
        double tx=32+i*36, ty=366+(i%3)*11;
        segment(c,tx,393,tx+3,ty-42,2.5,0x1b2d32,.72);
        oval(c,tx+3,ty-46,15+(i%3)*3,26,0x213d41,.74);
        oval(c,tx-7,ty-38,13,20,0x183036,.48);
    }
    const double glass1[]={54,31,78,31,174,388,151,388};
    polygon(c,glass1,4,0xffffff,.036);
    const double glass2[]={211,45,224,45,278,260,266,260};
    polygon(c,glass2,4,0xffffff,.020);
    cairo_restore(c);
    box(c,156,28,9,366,0x221d1c,1);
    box(c,161,29,1,365,0xb7845a,.45);
    box(c,32,207,264,9,0x231d1c,1);
    box(c,32,209,264,1.5,0xc59b6b,.35);
    round_box(c,12,398,314,16,3,0x2c2320,1);
    box(c,13,399,312,3,0xc79260,.72);
    round_box(c,19,408,303,8,3,0x161a1a,.7);
    radial_glow(c,293,368,300,0xe9a269,.10);
}

static void draw_poster(cairo_t *c,int which,double x,double y,double w,double h) {
    (void)which;
    const double pad=14;
    round_box(c,x+8,y+10,w,h,4,0x060b10,.40);
    round_box(c,x,y,w,h,4,0x12181b,1);
    round_box(c,x+3,y+3,w-6,h-6,2,0xb58f63,1);
    round_box(c,x+7,y+7,w-14,h-14,1,0x15191b,1);
    /* Empty, recessed mounts remain genuinely blank until playback supplies covers. */
    round_box(c,x+pad,y+pad,w-2*pad,w-2*pad,7,0x0d1418,1);
    box(c,x+pad+2,y+pad+2,w-2*pad-4,1,0x829099,.10);
    segment(c,x+5,y+h-7,x+5,y+6,1,0xffd8a0,.22);
    segment(c,x+5,y+6,x+w-5,y+6,1,0xffd8a0,.30);
}
static void draw_gallery(cairo_t *c) {
    draw_poster(c,0,370,42,178,228);
    draw_poster(c,1,571,42,178,228);
    draw_poster(c,2,772,42,178,228);
    for(int i=0;i<3;i++) {
        double cx=459.0+i*201;
        oval(c,cx,35,3,3,0x947d5b,.7);
        segment(c,cx,35,cx,42,1.2,0x8b7454,.47);
    }
}

static void draw_shelf(cairo_t *c) {
    const double x=311,y=312,w=680,h=183;
    round_box(c,x+12,y+14,w,h,5,0x0d1012,.42);
    linear_box(c,x,y,w,h,0,y,0,y+h,0x3d3027,0x1a1c1c);
    box(c,x+11,y+7,w-22,h-24,0x0d181d,1);
    box(c,x+16,y+13,w-32,10,0x000000,.18);
    box(c,x+223,y+7,13,h-12,0x6d4630,1);
    box(c,x+454,y+7,13,h-12,0x6d4630,1);
    box(c,x+224,y+9,2,h-17,0xce9459,.22);
    box(c,x+455,y+9,2,h-17,0xce9459,.22);
    for(int bay=0;bay<3;bay++) {
        double bx=x+19+bay*230, stop=bx+195;
        double cx=bx;
        int id=0;
        while(cx<stop) {
            uint32_t rnd=scramble(2801+(uint32_t)(bay*173+id*13));
            double width=5+(rnd%8), shift=(rnd>>8)%13;
            double height=144-shift;
            uint32_t palettes[]={0xa6614e,0xc69768,0x2d6867,0x66647d,
                                 0xb9a37e,0x6b474c,0x4b7161,0xc1a16a,
                                 0x916356,0x425764,0x9a7b58};
            uint32_t col=palettes[(rnd>>16)%11];
            if(cx+width>stop) width=stop-cx;
            box(c,cx,y+17+shift,width,height,col,1);
            box(c,cx+.6,y+18+shift,1,height-3,0xffffff,.18);
            box(c,cx+width-1.5,y+18+shift,1,height-3,0x061a20,.30);
            if(id%3==0) {
                box(c,cx+1,y+34+shift,width-2,2,0xeed6b3,.58);
                box(c,cx+1,y+127,width-2,1,0xeed6b3,.36);
            }
            if(id%5==0) {
                box(c,cx+width/2,y+50+shift,1,28,0xf3dfbc,.42);
                box(c,cx+width/2,y+83+shift,1,5,0xf3dfbc,.26);
            }
            cx+=width+1.7; id++;
        }
        box(c,bx-10,y+166,215,5,0x060f14,.85);
    }
    box(c,x-8,y-11,w+16,18,0x3c291e,1);
    linear_box(c,x-8,y-11,w+16,7,0,y-11,0,y-4,0x996a45,0x593925);
    box(c,x-8,y+172,w+16,17,0x5e3d2b,1);
    box(c,x-8,y+172,w+16,3,0xb17b4c,.7);
    box(c,x+9,y+189,w-18,5,0x151719,.8);
}

static void draw_table(cairo_t *c) {
    linear_box(c,0,487,1200,267,0,487,0,754,0x6d4b36,0x312927);
    radial_glow(c,190,510,530,0xf1ab6f,.11);
    radial_glow(c,1120,529,380,0xe6a156,.10);
    for(int i=0;i<52;i++) {
        double yy=493+i*5.0;
        uint32_t rnd=scramble((uint32_t)i+90);
        cairo_move_to(c,-30,yy);
        cairo_curve_to(c,250,yy-(rnd%10),620,yy+((rnd>>5)%13)-6,1220,yy-1);
        ink(c,i%7==0?0x241f1d:0xdfad76,i%7==0?.085:.042);
        cairo_set_line_width(c,i%9==0?1.6:.75); cairo_stroke(c);
    }
    /* A little specular life where the lamp and window hit the wood. */
    const double reflect1[]={0,509,242,509,262,546,0,547};
    polygon(c,reflect1,4,0xf8c792,.020);
    const double reflect2[]={910,520,1200,520,1200,573,965,566};
    polygon(c,reflect2,4,0xffd4a4,.018);
    segment(c,0,487,1200,487,2.7,0xd09b69,.42);
    box(c,0,751,1200,49,0x211c1c,1);
    box(c,0,751,1200,5,0x895633,.72);
    box(c,0,758,1200,2,0xe0a164,.15);
    for(int i=0;i<5;i++) segment(c,0,769+i*8,1200,769+i*8,1,0x8a5942,.09);
}

static void foliage(cairo_t *c,double x,double y,double len,double size,
                    double angle,uint32_t col) {
    cairo_save(c); cairo_translate(c,x,y); cairo_rotate(c,angle);
    cairo_move_to(c,0,0);
    cairo_curve_to(c,len*.17,-size*.60,len*.70,-size*.75,len,0);
    cairo_curve_to(c,len*.72,size*.64,len*.25,size*.63,0,0);
    cairo_close_path(c); ink(c,col,1); cairo_fill(c);
    cairo_move_to(c,3,0);
    cairo_curve_to(c,len*.3,-size*.12,len*.70,size*.07,len*.90,0);
    ink(c,0xb9be78,.26); cairo_set_line_width(c,1.0); cairo_stroke(c);
    cairo_restore(c);
}
static void draw_plant(cairo_t *c) {
    const double x=131,base=477;
    radial_glow(c,147,392,140,0x152e25,.19);
    /* A mature monstera with articulated stems instead of repeated ovals. */
    for(int i=0;i<11;i++) {
        double tx=x+((i*37)%110)-55;
        double ty=239+i*20;
        cairo_move_to(c,x,base-5);
        cairo_curve_to(c,x-24+i*5,base-113,x+((i*29)%95)-47,ty+57,tx,ty);
        ink(c,0x263f31,1); cairo_set_line_width(c,2.1+(i%3)*.65); cairo_stroke(c);
        foliage(c,tx,ty,70+(i%4)*12,32+(i%3)*5,
                (i%2)?2.95+(i%4)*.14:-.45+(i%3)*.15,
                i%3==0?0x46634b:(i%3==1?0x375d48:0x244b3f));
    }
    foliage(c,x+12,340,90,39,-1.20,0x41694a);
    foliage(c,x-19,306,85,34,3.95,0x2d5745);
    foliage(c,x+12,261,88,33,-.79,0x557459);
    /* Ground shadow and a tapered glazed terracotta pot. */
    oval(c,x+8,539,71,13,0x1a1717,.48);
    const double pot[]={89,463,173,463,160,524,153,534,106,534,99,524};
    polygon(c,pot,6,0x9c6b4f,1);
    const double potshade[]={146,467,174,467,160,524,153,534,143,534};
    polygon(c,potshade,5,0x614638,.48);
    oval(c,131,464,47,12,0xc18d66,1);
    oval(c,131,463,39,8,0x46372d,1);
    round_box(c,84,451,95,13,5,0xd29a6e,1);
    box(c,91,461,82,2,0x593f33,.5);
    segment(c,103,478,98,509,2,0xe3b990,.28);
}

static void draw_cup(cairo_t *c) {
    oval(c,82,651,63,15,0x171a1c,.36);
    /* Draw the handle first so the mug body can occlude part of it and make the
       connection feel physical rather than floating. */
    cairo_save(c);
    cairo_translate(c,92,607);
    cairo_scale(c,1.0,.86);
    cairo_arc(c,0,0,21,-SCENE_PI*.60,SCENE_PI*.48);
    ink(c,0xd9cdc0,1); cairo_set_line_width(c,8.8); cairo_stroke(c);
    cairo_arc(c,0,0,21,-SCENE_PI*.60,SCENE_PI*.48);
    ink(c,0x8c8479,.38); cairo_set_line_width(c,2.6); cairo_stroke(c);
    cairo_restore(c);

    cairo_pattern_t *p=cairo_pattern_create_linear(38,569,96,643);
    cairo_pattern_add_color_stop_rgb(p,0,SCENE_RGB(0xf3e6cf));
    cairo_pattern_add_color_stop_rgb(p,1,SCENE_RGB(0xa39a8f));
    cairo_move_to(c,40,582);cairo_line_to(c,95,582);cairo_line_to(c,92,635);
    cairo_curve_to(c,84,647,59,647,47,636);cairo_close_path(c);
    cairo_set_source(c,p);cairo_fill(c);cairo_pattern_destroy(p);

    /* Rim and coffee */
    oval(c,68,584,30.5,8.4,0xdfd3bf,1);
    oval(c,68,584,24.8,5.7,0x413128,1);
    oval(c,64,582,17.8,2.4,0x7c5f48,.43);

    /* Hide part of the handle with the body so it looks attached at both contact points. */
    box(c,86,590,10,28,0xcbbfae,.34);
    segment(c,53,602,49.5,626.5,1.8,0xffffff,.17);
    oval(c,90,604,9,3.2,0x1c1918,.09);
    oval(c,90,616,8,3.0,0x1c1918,.08);
}

static void draw_sleeve(cairo_t *c) {
    /* Leave a blank paper jacket here; the renderer adds live cover art or
       current track text when available. */

    /* One restrained contact shadow and the narrow thickness of a paper sleeve. */
    const double shadow[]={197,619,357,574,444,706,284,751};
    polygon(c,shadow,4,0x0a1014,.43);
    const double paper_edge[]={187,612,347,567,435,699,275,744};
    polygon(c,paper_edge,4,0xcbb898,1);
    segment(c,275,744,435,699,2.4,0x665444,.50);
    segment(c,347,567,435,699,1.6,0xffebcb,.36);

    cairo_save(c);
    cairo_matrix_t mat;
    /* 200x200 album sleeve mapped to tabletop parallelogram. */
    cairo_matrix_init(&mat,.80,-.225,.435,.66,186,608);
    cairo_transform(c,&mat);

    box(c,0,0,200,200,0xd8c9b0,1);
    box(c,6,6,188,188,0xb9aa92,1);
    box(c,9,9,182,182,0xc7b99f,1);
    segment(c,2,3,197,3,1.2,0xfff3d9,.60);
    segment(c,3,3,3,197,1.4,0xffffff,.28);
    cairo_restore(c);
}

static void draw_sideboard_and_lamp(cairo_t *c) {
    radial_glow(c,1090,290,244,0xffae5b,.25);
    radial_glow(c,1094,300,120,0xffd09a,.16);
    /* lamp spill reaching the wall and tabletop */
    const double spill[]={977,330,1198,350,1198,556,932,552};
    polygon(c,spill,4,0xffc37b,.06);
    box(c,1026,444,174,90,0x3b2b25,1);
    box(c,1021,435,179,14,0x815838,1);
    box(c,1021,435,179,3,0xe2a36a,.55);
    box(c,1038,459,150,3,0x190f12,.45);
    box(c,1038,496,150,2,0x1a1414,.54);
    oval(c,1109,480,3,3,0xe5bb76,.70);
    oval(c,1109,514,3,3,0xe5bb76,.70);
    oval(c,1098,440,48,10,0x161b1c,.38);
    oval(c,1092,431,38,9,0xb38858,1);
    oval(c,1092,428,27,6,0x694a32,1);
    round_box(c,1085,290,13,139,5,0xa5774a,1);
    box(c,1088,299,3,125,0xe1b783,.52);
    oval(c,1092,293,10,8,0xe2b17a,1);
    const double lamp[]={1044,253,1146,253,1180,328,1011,328};
    polygon(c,lamp,4,0xe5c79a,1);
    const double lampleft[]={1044,253,1080,253,1055,328,1011,328};
    polygon(c,lampleft,4,0xffebb9,.34);
    const double lampright[]={1114,253,1146,253,1180,328,1131,328};
    polygon(c,lampright,4,0xb27d57,.35);
    const double lampinner[]={1058,263,1134,263,1159,318,1032,318};
    polygon(c,lampinner,4,0xffdd9e,.21);
    oval(c,1095,253,51,7,0xc7a278,1);
    oval(c,1095,328,85,9,0xc08b5c,1);
    oval(c,1095,326,79,5,0xffdc9a,.53);
    segment(c,1021,326,1169,326,1.5,0xffdfa4,.40);
}

void draw_turntable_tonearm(cairo_t *c, double y_scale) {
    cairo_save(c);
    cairo_scale(c, 1.0, y_scale);
    oval(c,1043,414,35,15,0x080d11,.88);
    oval(c,1043,405,29,19,0xb1a994,1);
    oval(c,1043,400,23,13,0x5c6262,1);
    oval(c,1043,399,15,8,0xd2c4a6,1);
    oval_ring(c,1043,402,26,15,.085,0xf3daba,.28);
    cairo_set_line_cap(c,CAIRO_LINE_CAP_ROUND);
    segment(c,1044,405,1104,391,8,0x252c2e,1);
    segment(c,1091,392,1116,387,22,0x636969,1);
    segment(c,1091,390,1116,386,5,0xccbca0,.55);
    cairo_move_to(c,1042,408);
    cairo_curve_to(c,1039,419,1005,423,982,430);
    cairo_curve_to(c,963,434,948,438,941,442);
    ink(c,0x151b1d,.78);cairo_set_line_width(c,11);cairo_stroke(c);
    cairo_move_to(c,1042,405);
    cairo_curve_to(c,1038,416,1005,420,982,427);
    cairo_curve_to(c,965,432,950,436,942,439);
    ink(c,0xc0b39a,1);cairo_set_line_width(c,6);cairo_stroke(c);
    cairo_move_to(c,1041,403);
    cairo_curve_to(c,1038,413,1005,418,982,425);
    cairo_curve_to(c,966,430,952,433,942,437);
    ink(c,0xf7e6bc,.50);cairo_set_line_width(c,1.4);cairo_stroke(c);
    oval(c,946,446,26,6,0x000000,.12);
    cairo_save(c);cairo_translate(c,942,441);cairo_rotate(c,-.22);
    round_box(c,-11,-5,23,11,2,0x252f34,1);
    box(c,-9,-3,14,3,0xf0cda1,.72);
    segment(c,-4,6,-3,11,1.4,0xddd2bb,.78);
    cairo_restore(c);
    cairo_restore(c);
}

static void draw_turntable(cairo_t *c) {
    const double cx=771.6,cy=452.8,rx=205.2,ry=48.0;
    oval(c,823,667,397,32,0x101618,.31);
    radial_glow(c,805,610,350,0x151717,.11);
    const double base[]={411,551,1190,551,1183,660,431,660};
    polygon(c,base,4,0x392924,1);
    const double front[]={418,564,1188,564,1180,646,427,646};
    polygon(c,front,4,0x744a31,1);
    const double frontbelow[]={427,616,1184,616,1180,646,427,646};
    polygon(c,frontbelow,4,0x422a22,.58);
    for(int i=0;i<14;i++) {
        double yy=573+i*4.8;
        segment(c,434,yy,1174,yy,1,i%3?0xb1794a:0x2d211e,i%3?.085:.13);
    }
    const double bevel[]={477,389,1093,389,1190,561,413,561};
    polygon(c,bevel,4,0x956541,1);
    const double brassfront[]={415,560,1188,560,1185,566,419,566};
    polygon(c,brassfront,4,0xd09a5a,.70);
    const double woodtop[]={481,393,1089,393,1181,555,423,555};
    polygon(c,woodtop,4,0x43352d,1);
    const double controlpanel[]={492,401,1081,401,1167,547,437,547};
    polygon(c,controlpanel,4,0x141c22,1);
    const double gloss1[]={570,407,960,410,1060,533,676,527};
    polygon(c,gloss1,4,0xffffff,.018);
    segment(c,491,402,1083,402,1.5,0xd2aa76,.42);
    segment(c,439,546,1165,546,1.2,0xe9ae71,.30);
    segment(c,492,402,438,545,1.8,0x5a6060,.45);
    segment(c,1082,402,1166,546,1.8,0x5a6060,.38);
    oval(c,cx+4,cy+6,rx+17,ry+10,0x080e12,.72);
    oval(c,cx,cy+2,rx+12,ry+6,0x42474a,1);
    oval_ring(c,cx,cy+1,rx+10,ry+5,.028,0xbeb49b,1);
    oval(c,cx,cy,rx+6,ry+2,0x77776f,1);
    oval(c,cx,cy-1,rx,ry,0x080e13,1);
    cairo_save(c);cairo_translate(c,cx,cy-1);cairo_scale(c,rx,ry);
    cairo_pattern_t *vinyl=cairo_pattern_create_linear(-1,-1,1,1);
    cairo_pattern_add_color_stop_rgb(vinyl,0,SCENE_RGB(0x31393c));
    cairo_pattern_add_color_stop_rgb(vinyl,.35,SCENE_RGB(0x0d151a));
    cairo_pattern_add_color_stop_rgb(vinyl,.75,SCENE_RGB(0x182228));
    cairo_pattern_add_color_stop_rgb(vinyl,1,SCENE_RGB(0x080c11));
    cairo_arc(c,0,0,.976,0,2*SCENE_PI);
    cairo_set_source(c,vinyl);cairo_fill(c);cairo_pattern_destroy(vinyl);
    for(int i=0;i<27;i++) {
        double rr=.30+i*.0245;
        cairo_arc(c,0,0,rr,0,2*SCENE_PI);
        ink(c,i%5==0?0xd6af87:0xdce2d5,i%5==0?.11:.045);
        cairo_set_line_width(c,.0035);cairo_stroke(c);
    }
    cairo_restore(c);
    /* specular streaks on the vinyl for a more physical read */
    oval(c,cx-66,cy-8,52,8,0xffffff,.03);
    oval(c,cx+74,cy+10,66,9,0xffffff,.016);
    /* Matching navy + champagne centre label ties the played record to its jacket. */
    oval(c,cx,cy-1,45,11,0xe0c59e,1);
    oval(c,cx,cy-1,40,9,0x173846,1);
    oval_ring(c,cx,cy-1,38,9,.04,0xe7b989,.30);
    oval(c,cx,cy-1,9,2.7,0x302b2a,1);
    oval(c,cx,cy-1,2.8,1.6,0xded8c3,1);
    oval(c,1078,505,29,11,0x0a1014,.78);
    oval(c,1078,501,26,10,0x9b9684,1);
    oval(c,1078,498,22,8,0x41494b,1);
    oval(c,1078,497,16,5,0x6c7472,.55);
    oval(c,1135,527,16,9,0x101618,1);
    oval(c,1135,525,12,6,0xb79b74,1);
    oval(c,1135,524,7,3,0x493e34,1);
    oval(c,1037,529,4,2,0x89d6a9,1);
    radial_glow(c,1037,528,27,0x80e0a2,.20);
    draw_turntable_tonearm(c, 1.0);
    segment(c,429,571,1175,571,1.3,0xf1bd81,.38);
    segment(c,432,640,1176,640,1.5,0x191719,.56);
    box(c,913,589,1,45,0xd9a36d,.35);
    oval(c,1125,610,15,15,0x241c1a,1);
    oval(c,1125,607,11,11,0x987959,1);
    oval(c,1125,605,6,6,0xd1ad78,1);
    oval(c,1147,605,3.2,3.2,0x9ed2a4,1);
    radial_glow(c,1147,605,20,0x8ed8a1,.22);
    round_box(c,463,647,43,16,4,0x131718,.85);
    round_box(c,1107,646,42,15,4,0x131718,.85);
}

static void atmosphere(cairo_t *c) {
    for(int i=0;i<72;i++) {
        uint32_t n=scramble((uint32_t)i*103+461);
        double x=(n%1200),y=((n>>11)%735);
        double a=(x<300)?.08:.026;
        oval(c,x,y,(n>>24)%3==0?1.4:.7,.7,0xffe4b9,a);
    }
    /* Slight filmic grade ties the illustrative primitives together. */
    box(c,0,0,1200,800,0x271f18,.045);
    cairo_pattern_t *p=cairo_pattern_create_radial(625,372,160,625,372,860);
    cairo_pattern_add_color_stop_rgba(p,0,0,0,0,0);
    cairo_pattern_add_color_stop_rgba(p,.70,0,0,0,.04);
    cairo_pattern_add_color_stop_rgba(p,1,0,0,0,.42);
    cairo_rectangle(c,0,0,1200,800);cairo_set_source(c,p);cairo_fill(c);
    cairo_pattern_destroy(p);
}

void draw_programmatic_scene(cairo_t *cr,double w,double h) {
    if(!cr||w<=0||h<=0) return;
    cairo_save(cr);
    cairo_scale(cr,w/1200.0,h/800.0);
    draw_wall(cr);
    draw_window(cr);
    draw_gallery(cr);
    draw_shelf(cr);
    draw_table(cr);
    draw_sideboard_and_lamp(cr);
    draw_plant(cr);
    draw_cup(cr);
    draw_sleeve(cr);
    draw_turntable(cr);
    atmosphere(cr);
    cairo_restore(cr);
}
