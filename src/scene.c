#include <cairo.h>
#include <math.h>
#include "needle.h"

/* A hand drawn listening room. Artwork is supplied separately by the player. */
static void round_path(cairo_t *cr, double x, double y, double w, double h, double r) {
    r = MIN(r, MIN(w, h) * .5);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-r, y+r, r, -G_PI_2, 0);
    cairo_arc(cr, x+w-r, y+h-r, r, 0, G_PI_2);
    cairo_arc(cr, x+r, y+h-r, r, G_PI_2, G_PI);
    cairo_arc(cr, x+r, y+r, r, G_PI, 3*G_PI_2);
    cairo_close_path(cr);
}

static void rect(cairo_t *cr, double x, double y, double w, double h,
                 double r, double g, double b, double a) {
    cairo_rectangle(cr, x, y, w, h); cairo_set_source_rgba(cr, r, g, b, a); cairo_fill(cr);
}

static void ellipse(cairo_t *cr, double cx, double cy, double rx, double ry,
                    double r, double g, double b, double a) {
    cairo_save(cr); cairo_translate(cr, cx, cy); cairo_scale(cr, rx, ry);
    cairo_arc(cr, 0, 0, 1, 0, 2*G_PI); cairo_set_source_rgba(cr, r, g, b, a); cairo_fill(cr);
    cairo_restore(cr);
}

static void draw_window(cairo_t *cr, double w, double h) {
    double x=0, y=0, ww=w*.218, hh=h*.45;
    cairo_save(cr);
    rect(cr, x, y, ww, hh, .12,.075,.052,1);
    cairo_rectangle(cr, 13, 12, ww-29, hh-27);
    cairo_pattern_t *sky=cairo_pattern_create_linear(0,0,ww,hh);
    cairo_pattern_add_color_stop_rgb(sky,0,.21,.28,.38);
    cairo_pattern_add_color_stop_rgb(sky,.45,.95,.48,.25);
    cairo_pattern_add_color_stop_rgb(sky,1,.97,.66,.32);
    cairo_set_source(cr,sky); cairo_fill(cr); cairo_pattern_destroy(sky);
    /* Soft sunset glow and simple distant tree silhouettes. */
    cairo_pattern_t *glow=cairo_pattern_create_radial(ww*.72,hh*.55,2,ww*.72,hh*.55,ww*.48);
    cairo_pattern_add_color_stop_rgba(glow,0,1,.78,.42,.85); cairo_pattern_add_color_stop_rgba(glow,1,1,.53,.26,0);
    cairo_rectangle(cr,14,13,ww-30,hh-28); cairo_set_source(cr,glow); cairo_fill(cr); cairo_pattern_destroy(glow);
    cairo_save(cr); cairo_rectangle(cr,14,13,ww-30,hh-28); cairo_clip(cr);
    for(int i=0;i<11;i++) {
        double tx=ww*(.02+i*.105), base=hh*(.82+(i%3)*.035), th=hh*(.18+(i%4)*.04);
        ellipse(cr,tx,base-th*.45,ww*.055,th*.48,.13,.20,.16,.52);
        rect(cr,tx-ww*.006,base-th*.35,ww*.012,th*.48,.11,.12,.10,.8);
    }
    cairo_restore(cr);
    rect(cr,ww*.49,0,ww*.028,hh,.10,.065,.045,1);
    rect(cr,0,hh*.50,ww,hh*.027,.10,.065,.045,1);
    /* Wood sill and a narrow light-catching edge. */
    rect(cr,0,hh-9,ww+25,18,.24,.12,.055,1);
    rect(cr,0,hh-11,ww+24,3,.69,.38,.16,.9);
    cairo_restore(cr);
}

static void leaf(cairo_t *cr,double x,double y,double sx,double sy,double angle,double shade) {
    cairo_save(cr); cairo_translate(cr,x,y); cairo_rotate(cr,angle);
    cairo_move_to(cr,0,0); cairo_curve_to(cr,sx*.22,-sy*.72,sx*.84,-sy*.52,sx,0);
    cairo_curve_to(cr,sx*.78,sy*.56,sx*.20,sy*.58,0,0); cairo_close_path(cr);
    cairo_pattern_t *p=cairo_pattern_create_linear(0,-sy*.4,sx,sy*.3);
    cairo_pattern_add_color_stop_rgb(p,0,.18+shade,.29+shade,.15);
    cairo_pattern_add_color_stop_rgb(p,1,.035,.12+shade,.065);
    cairo_set_source(cr,p); cairo_fill(cr); cairo_pattern_destroy(p);
    cairo_move_to(cr,2,0); cairo_curve_to(cr,sx*.35,-sy*.02,sx*.60,-sy*.01,sx*.91,0);
    cairo_set_source_rgba(cr,.65,.62,.32,.28); cairo_set_line_width(cr,1.2); cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_plant(cairo_t *cr,double w,double h) {
    double x=w*.09, floor=h*.61;
    /* Stems and broad, individually controlled leaves. */
    cairo_set_line_width(cr,4); cairo_set_source_rgb(cr,.11,.20,.10);
    cairo_move_to(cr,x,floor-h*.10); cairo_curve_to(cr,x-28,floor-h*.26,x+52,floor-h*.40,x+20,floor-h*.48); cairo_stroke(cr);
    cairo_move_to(cr,x,floor-h*.10); cairo_curve_to(cr,x+35,floor-h*.26,x-40,floor-h*.36,x-16,floor-h*.45); cairo_stroke(cr);
    leaf(cr,x+20,floor-h*.46,68,29,-.44,.07); leaf(cr,x-14,floor-h*.43,72,31,3.65,.02);
    leaf(cr,x+30,floor-h*.39,75,31,-.18,.10); leaf(cr,x-8,floor-h*.36,67,30,3.45,.07);
    leaf(cr,x+44,floor-h*.31,70,32,.28,.04); leaf(cr,x-25,floor-h*.31,76,33,3.95,.03);
    leaf(cr,x+29,floor-h*.25,78,34,-.42,.10); leaf(cr,x-8,floor-h*.24,64,28,3.3,.05);
    leaf(cr,x+12,floor-h*.20,78,34,-.12,.08); leaf(cr,x-39,floor-h*.20,67,29,3.6,.01);
    leaf(cr,x+48,floor-h*.16,62,28,.25,.06); leaf(cr,x-25,floor-h*.14,68,30,3.35,.08);
    leaf(cr,x+5,floor-h*.37,57,25,1.1,.02); leaf(cr,x-2,floor-h*.40,59,25,2.0,.07);
    ellipse(cr,x+8,floor-h*.11,66,15,.04,.025,.018,.30);
    cairo_pattern_t *pot=cairo_pattern_create_linear(x-46,floor-h*.13,x+52,floor);
    cairo_pattern_add_color_stop_rgb(pot,0,.72,.48,.29); cairo_pattern_add_color_stop_rgb(pot,1,.36,.20,.10);
    cairo_move_to(cr,x-45,floor-h*.12); cairo_line_to(cr,x+46,floor-h*.12); cairo_line_to(cr,x+36,floor); cairo_curve_to(cr,x+5,floor+9,x-14,floor+9,x-37,floor); cairo_close_path(cr);
    cairo_set_source(cr,pot); cairo_fill(cr); cairo_pattern_destroy(pot);
    ellipse(cr,x,floor-h*.12,47,10,.79,.58,.36,1);
    cairo_set_source_rgba(cr,.96,.73,.47,.35); cairo_set_line_width(cr,2);
    cairo_move_to(cr,x-34,floor-h*.085); cairo_line_to(cr,x+35,floor-h*.085); cairo_stroke(cr);
}

static void draw_frames(cairo_t *cr,double w,double h) {
    const double centers[]={.452,.656,.860};
    double y=h*.045, fw=w*.178, fh=h*.223;
    for(int i=0;i<3;i++) {
        double x=w*centers[i]-fw*.5;
        /* cast shadow, walnut frame, cream mat and recessed art opening */
        round_path(cr,x+8,y+10,fw,fh,3); cairo_set_source_rgba(cr,.03,.018,.012,.46); cairo_fill(cr);
        cairo_pattern_t *wood=cairo_pattern_create_linear(x,y,x+fw,y+fh);
        cairo_pattern_add_color_stop_rgb(wood,0,.24,.12,.065); cairo_pattern_add_color_stop_rgb(wood,.45,.09,.045,.025); cairo_pattern_add_color_stop_rgb(wood,1,.32,.17,.08);
        round_path(cr,x,y,fw,fh,3); cairo_set_source(cr,wood); cairo_fill(cr); cairo_pattern_destroy(wood);
        rect(cr,x+fw*.055,y+fh*.085,fw*.89,fh*.83,.41,.29,.20,1);
        rect(cr,x+fw*.10,y+fh*.15,fw*.80,fh*.70,.20,.12,.08,1);
        rect(cr,x+fw*.115,y+fh*.17,fw*.77,fh*.66,.11,.065,.043,1);
        /* Fine highlights make the frames read as objects on the wall. */
        cairo_set_source_rgba(cr,.82,.56,.30,.45); cairo_set_line_width(cr,1.4);
        cairo_move_to(cr,x+5,y+fh-5); cairo_line_to(cr,x+5,y+6); cairo_line_to(cr,x+fw-6,y+6); cairo_stroke(cr);
    }
}

static void draw_shelf(cairo_t *cr,double w,double h) {
    double x=w*.31,y=h*.355,ww=w*.49,hh=h*.235;
    rect(cr,x-10,y+10,ww+20,hh,.05,.025,.015,.34);
    cairo_pattern_t *dark=cairo_pattern_create_linear(x,y,x,y+hh);
    cairo_pattern_add_color_stop_rgb(dark,0,.13,.07,.04); cairo_pattern_add_color_stop_rgb(dark,1,.025,.018,.013);
    cairo_rectangle(cr,x,y,ww,hh); cairo_set_source(cr,dark); cairo_fill(cr); cairo_pattern_destroy(dark);
    rect(cr,x-14,y-13,ww+28,22,.34,.17,.075,1);
    rect(cr,x-14,y-14,ww+28,4,.68,.34,.14,1);
    rect(cr,x-12,y+hh-10,ww+24,15,.25,.12,.05,1);
    rect(cr,x+ww*.50,y+6,8,hh-18,.18,.09,.045,1);
    /* LP spines are simple varied wood, paper and faded-color strips. */
    unsigned int seed=773;
    for(int bay=0;bay<2;bay++) {
        double bx=x+18+bay*(ww*.50), bw=ww*.50-28;
        for(double xx=bx;xx<bx+bw;) {
            seed=seed*1664525u+1013904223u;
            double sw=9+(seed%13), inset=3+(seed%9);
            double r=.16+((seed>>8)%35)/100.0, g=.09+((seed>>16)%25)/100.0, b=.045+((seed>>24)%20)/100.0;
            rect(cr,xx,y+inset,MIN(sw,bx+bw-xx),hh-25,r,g,b,1);
            rect(cr,xx+1,y+inset+4,1.5,hh-34,.78,.54,.30,.35);
            if((seed&3)==0) rect(cr,xx+3,y+hh*.30,sw-5,2,.76,.48,.22,.42);
            xx+=sw+2;
        }
    }
}

static void draw_lamp(cairo_t *cr,double w,double h) {
    double x=w*.963, y=h*.295;
    /* Small side cabinet grounds the lamp and adds a second plane behind the deck. */
    rect(cr,w*.895,h*.49,w*.105,h*.12,.19,.085,.035,1);
    rect(cr,w*.886,h*.48,w*.114,h*.018,.40,.20,.085,1);
    rect(cr,w*.895,h*.495,w*.105,3,.70,.38,.16,.8);
    rect(cr,w*.91,h*.535,w*.07,2,.075,.032,.014,.85);
    rect(cr,w*.91,h*.575,w*.07,2,.075,.032,.014,.85);
    ellipse(cr,w*.947,h*.535,4,4,.58,.36,.14,.8);
    ellipse(cr,w*.947,h*.575,4,4,.58,.36,.14,.8);
    cairo_pattern_t *halo=cairo_pattern_create_radial(x,y,10,x,y,w*.19);
    cairo_pattern_add_color_stop_rgba(halo,0,1,.59,.24,.25); cairo_pattern_add_color_stop_rgba(halo,1,1,.48,.18,0);
    cairo_rectangle(cr,x-w*.22,y-w*.20,w*.42,w*.46); cairo_set_source(cr,halo); cairo_fill(cr); cairo_pattern_destroy(halo);
    /* table edge, turned wooden stem and low round base */
    ellipse(cr,x,y+h*.19,92,15,.17,.08,.035,1);
    rect(cr,x-12,y+h*.04,24,h*.17,.24,.11,.045,1);
    ellipse(cr,x,y+h*.21,39,9,.11,.055,.025,1);
    cairo_pattern_t *shade=cairo_pattern_create_linear(x-w*.055,y-h*.08,x+w*.07,y+h*.10);
    cairo_pattern_add_color_stop_rgb(shade,0,1,.83,.53); cairo_pattern_add_color_stop_rgb(shade,1,.88,.43,.14);
    cairo_move_to(cr,x-w*.115,y+h*.055); cairo_curve_to(cr,x-w*.095,y-h*.06,x+w*.13,y-h*.06,x+w*.14,y+h*.055);
    cairo_curve_to(cr,x+w*.11,y+h*.085,x-w*.08,y+h*.085,x-w*.115,y+h*.055); cairo_close_path(cr);
    cairo_set_source(cr,shade); cairo_fill(cr); cairo_pattern_destroy(shade);
    ellipse(cr,x+5,y+h*.055,13,5,1,.88,.61,.95);
    cairo_set_source_rgba(cr,.48,.22,.065,.75); cairo_set_line_width(cr,2); cairo_stroke(cr);
}

static void draw_desk(cairo_t *cr,double w,double h) {
    double top=h*.57;
    cairo_pattern_t *wood=cairo_pattern_create_linear(0,top,0,h);
    cairo_pattern_add_color_stop_rgb(wood,0,.40,.22,.105); cairo_pattern_add_color_stop_rgb(wood,.27,.55,.30,.14); cairo_pattern_add_color_stop_rgb(wood,1,.22,.095,.04);
    cairo_rectangle(cr,0,top,w,h-top); cairo_set_source(cr,wood); cairo_fill(cr); cairo_pattern_destroy(wood);
    for(int i=0;i<30;i++) {
        double yy=top+13+i*(h-top-20)/30.0;
        cairo_set_source_rgba(cr,(i%3==0)?1:.13,(i%3==0)?.60:.055,(i%3==0)?.27:.018,(i%3==0)?.10:.16);
        cairo_set_line_width(cr,(i%5==0)?2.0:.8);
        cairo_move_to(cr,0,yy); cairo_curve_to(cr,w*.28,yy-5,w*.70,yy+8,w,yy-2); cairo_stroke(cr);
    }
    rect(cr,0,h*.93,w,h*.07,.14,.055,.023,1);
    rect(cr,0,h*.93,w,5,.64,.32,.13,.85);
}

static void draw_turntable(cairo_t *cr,double w,double h) {
    double x=w*.432,y=h*.515,ww=w*.548,hh=h*.328;
    /* Soft tabletop shadow and a handsome walnut plinth. */
    round_path(cr,x+8,y+hh*.82,ww,hh*.28,14); cairo_set_source_rgba(cr,.025,.012,.006,.55); cairo_fill(cr);
    cairo_pattern_t *side=cairo_pattern_create_linear(0,y+hh*.52,0,y+hh*1.1);
    cairo_pattern_add_color_stop_rgb(side,0,.39,.17,.065); cairo_pattern_add_color_stop_rgb(side,.45,.25,.085,.027); cairo_pattern_add_color_stop_rgb(side,1,.11,.035,.014);
    round_path(cr,x+5,y+hh*.59,ww-10,hh*.47,13); cairo_set_source(cr,side); cairo_fill(cr); cairo_pattern_destroy(side);
    cairo_pattern_t *top=cairo_pattern_create_linear(x,y,x+ww,y+hh);
    cairo_pattern_add_color_stop_rgb(top,0,.46,.23,.095); cairo_pattern_add_color_stop_rgb(top,.50,.20,.09,.035); cairo_pattern_add_color_stop_rgb(top,1,.35,.16,.06);
    round_path(cr,x,y,ww,hh*.66,14); cairo_set_source(cr,top); cairo_fill(cr); cairo_pattern_destroy(top);
    cairo_set_source_rgba(cr,.84,.46,.18,.56); cairo_set_line_width(cr,2);
    round_path(cr,x+4,y+3,ww-8,hh*.64,12); cairo_stroke(cr);
    /* black control deck */
    round_path(cr,x+ww*.035,y+hh*.07,ww*.93,hh*.52,10);
    cairo_pattern_t *panel=cairo_pattern_create_linear(0,y,0,y+hh*.6);
    cairo_pattern_add_color_stop_rgb(panel,0,.12,.115,.10); cairo_pattern_add_color_stop_rgb(panel,1,.025,.023,.021);
    cairo_set_source(cr,panel); cairo_fill(cr); cairo_pattern_destroy(panel);
    /* platter rim and exposed brushed-metal edge, aligned to the record overlay */
    double cx=w*.643,cy=h*.566,rx=w*.171,ry=h*.060;
    ellipse(cr,cx,cy+4,rx*1.045,ry*1.14,.07,.035,.015,.72);
    ellipse(cr,cx,cy,rx*1.035,ry*1.10,.63,.43,.20,1);
    ellipse(cr,cx,cy-1,rx*1.012,ry*1.035,.78,.66,.43,1);
    ellipse(cr,cx,cy-2,rx*.988,ry*.98,.12,.105,.09,1);
    ellipse(cr,cx,cy-3,rx*.975,ry*.94,.035,.034,.031,1);
    /* tonearm pivot tower, counterweight and the arm running onto the record */
    ellipse(cr,w*.842,h*.510,w*.028,h*.017,.055,.045,.034,1);
    ellipse(cr,w*.842,h*.496,w*.021,h*.031,.29,.25,.19,1);
    ellipse(cr,w*.842,h*.482,w*.018,h*.022,.63,.53,.37,1);
    ellipse(cr,w*.862,h*.469,w*.026,h*.027,.22,.205,.17,1);
    ellipse(cr,w*.862,h*.469,w*.021,h*.022,.46,.40,.30,1);
    cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr,w*.842,h*.506); cairo_line_to(cr,w*.784,h*.535);
    cairo_set_source_rgb(cr,.62,.53,.38); cairo_set_line_width(cr,w*.007); cairo_stroke(cr);
    cairo_move_to(cr,w*.842,h*.506); cairo_line_to(cr,w*.784,h*.535);
    cairo_set_source_rgba(cr,.95,.83,.61,.88); cairo_set_line_width(cr,w*.0015); cairo_stroke(cr);
    /* controls and switch on the exposed right side of the deck */
    ellipse(cr,w*.935,h*.535,w*.012,h*.013,.055,.048,.037,1);
    ellipse(cr,w*.935,h*.530,w*.008,h*.007,.52,.44,.31,1);
    ellipse(cr,w*.904,h*.600,w*.010,h*.009,.08,.065,.045,1);
    ellipse(cr,w*.904,h*.596,w*.006,h*.005,.73,.61,.40,1);
    cairo_set_source_rgb(cr,.68,.55,.37); cairo_set_line_width(cr,2.3);
    cairo_move_to(cr,w*.865,h*.605); cairo_line_to(cr,w*.880,h*.592); cairo_stroke(cr);
    rect(cr,x+ww*.04,y+hh*.59,ww*.92,2,.70,.40,.17,.62);
}

void draw_programmatic_scene(cairo_t *cr,double w,double h) {
    cairo_pattern_t *wall=cairo_pattern_create_linear(0,0,0,h*.60);
    cairo_pattern_add_color_stop_rgb(wall,0,.25,.17,.12); cairo_pattern_add_color_stop_rgb(wall,.55,.43,.28,.19); cairo_pattern_add_color_stop_rgb(wall,1,.32,.19,.11);
    cairo_rectangle(cr,0,0,w,h*.62); cairo_set_source(cr,wall); cairo_fill(cr); cairo_pattern_destroy(wall);
    /* fine plaster striations */
    for(int i=0;i<120;i++) {
        double xx=(i*173)% (int)w, yy=(i*97)%(int)(h*.56);
        cairo_set_source_rgba(cr,.98,.73,.47,(i%4==0)?.025:.012);
        cairo_set_line_width(cr,1); cairo_move_to(cr,xx,yy); cairo_line_to(cr,xx+20+(i%18),yy+((i%3)-1)*2); cairo_stroke(cr);
    }
    draw_window(cr,w,h);
    draw_frames(cr,w,h);
    draw_shelf(cr,w,h);
    draw_lamp(cr,w,h);
    draw_desk(cr,w,h);
    draw_plant(cr,w,h);
    draw_turntable(cr,w,h);
    /* gentle overall edge falloff */
    cairo_pattern_t *shade=cairo_pattern_create_radial(w*.52,h*.48,w*.14,w*.52,h*.48,w*.75);
    cairo_pattern_add_color_stop_rgba(shade,0,0,0,0,0); cairo_pattern_add_color_stop_rgba(shade,1,.02,.012,.006,.35);
    cairo_rectangle(cr,0,0,w,h); cairo_set_source(cr,shade); cairo_fill(cr); cairo_pattern_destroy(shade);
}
