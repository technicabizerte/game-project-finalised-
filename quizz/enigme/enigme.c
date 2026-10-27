#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define BG_MENU_PATH    "photo1.png"
#define BG_QUIZ_PATH    "photo2.png"
#define MUSIC_QUIZ      "son.mp3"
#define FONT_PATH       "font.ttf"
#define QUESTIONS_FILE  "questions.txt"
#define FONT_TITLE      40
#define FONT_SMALL      24
#define FONT_BIG        60
#define FEEDBACK_MS     900
#define RTZ_MS          1100
#define MAX_QUESTIONS   50
#define MAX_PARTICLES   60
#define POINTS_BASE     10
#define POINTS_SPEED    5
#define POINTS_STREAK   5
#define STREAK_NEEDED   3
#define NB_LEVELS       3
#define MAX_VIES        6
#define BONUS_TEMPS_MS  1000

typedef struct {
    SDL_Rect rect;
    char     label[512];
    int      hovered;
} Button;

typedef struct {
    SDL_Rect    rect;
    SDL_Texture *normal;
    SDL_Texture *hover;
    int          is_hover;
} ImgButton;

typedef struct {
    char question[512];
    char answers[3][256];
    int  correct;
    int  vu;
} QuizQ;

typedef enum {
    ST_MENU=0, ST_DIFFICULTY=1, ST_QUIZ=2, ST_RESULTS=3, ST_GAMEOVER=4
} State;

typedef struct {
    const char *name;
    Uint32      chrono_ms;
    int         nb_q;
    SDL_Color   color;
    const char *desc;
} LevelConfig;

static const LevelConfig LEVELS[NB_LEVELS] = {
    { "FACILE",    15000, 10, {80,200,80,255},  "15 sec / question  -  10 questions" },
    { "MOYEN",     10000, 15, {200,160,40,255}, "10 sec / question  -  15 questions" },
    { "DIFFICILE",  6000, 15, {200,60,60,255},  " 6 sec / question  -  15 questions" },
};

typedef struct {
    float x,y,vx,vy,size,alpha;
    int   active;
} Particle;

static Particle particles[MAX_PARTICLES];

static void particles_spawn(int win_w, int win_h){
    for(int i=0;i<MAX_PARTICLES;i++){
        particles[i].x     =(float)(rand()%win_w);
        particles[i].y     =(float)(rand()%(win_h/2));
        particles[i].vx    =((float)(rand()%100)-50)/120.0f;
        particles[i].vy    =(float)(rand()%40+15)/60.0f;
        particles[i].size  =(float)(rand()%5+3);
        particles[i].alpha =(float)(rand()%80+170);
        particles[i].active=1;
    }
}

static void particles_update(int win_h){
    for(int i=0;i<MAX_PARTICLES;i++){
        if(!particles[i].active)continue;
        particles[i].x    +=particles[i].vx;
        particles[i].y    +=particles[i].vy;
        particles[i].alpha-=1.2f;
        if(particles[i].alpha<=0||particles[i].y>win_h)
            particles[i].active=0;
    }
}

static void particles_draw(SDL_Renderer *r){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    for(int i=0;i<MAX_PARTICLES;i++){
        if(!particles[i].active)continue;
        int s=(int)particles[i].size;
        SDL_SetRenderDrawColor(r,200,230,255,(Uint8)particles[i].alpha);
        SDL_Rect rc={(int)particles[i].x-s/2,(int)particles[i].y-s/2,s,s};
        SDL_RenderFillRect(r,&rc);
        SDL_SetRenderDrawColor(r,255,255,255,(Uint8)particles[i].alpha);
        SDL_Rect dot={(int)particles[i].x-1,(int)particles[i].y-1,2,2};
        SDL_RenderFillRect(r,&dot);
    }
}

static int    displayed_score=0;
static int    target_score=0;
static Uint32 score_anim_t=0;

static QuizQ quizList[MAX_QUESTIONS];
static int   NB_QUESTIONS=0;

static int load_questions(const char *path){
    FILE *f=fopen(path,"r");
    if(!f){fprintf(stderr,"[ENIGME] Impossible d'ouvrir %s\n",path);return 0;}
    char line[512];int count=0;
    while(count<MAX_QUESTIONS){
        QuizQ q;int got_q=0,got_a=0,got_c=0;
        while(fgets(line,sizeof(line),f)){
            line[strcspn(line,"\r\n")]=0;
            if(strlen(line)>0){strncpy(q.question,line,sizeof(q.question)-1);q.question[sizeof(q.question)-1]=0;got_q=1;break;}
        }
        if(!got_q)break;
        while(got_a<3&&fgets(line,sizeof(line),f)){
            line[strcspn(line,"\r\n")]=0;
            if(strlen(line)>0){strncpy(q.answers[got_a],line,sizeof(q.answers[0])-1);q.answers[got_a][sizeof(q.answers[0])-1]=0;got_a++;}
        }
        if(got_a<3)break;
        while(fgets(line,sizeof(line),f)){
            line[strcspn(line,"\r\n")]=0;
            if(strlen(line)>0){q.correct=atoi(line);got_c=1;break;}
        }
        if(!got_c)break;
        q.vu=0;
        quizList[count++]=q;
    }
    fclose(f);
    fprintf(stdout,"[ENIGME] %d questions chargees\n",count);
    return count;
}

int enigme_run(SDL_Window *window, SDL_Renderer *renderer);

static int in_rect(int x,int y,const SDL_Rect *r){
    return(x>=r->x&&x<r->x+r->w&&y>=r->y&&y<r->y+r->h);
}
static SDL_Texture *load_texture(SDL_Renderer *r,const char *path){
    SDL_Surface *s=IMG_Load(path);
    if(!s){fprintf(stderr,"IMG_Load(%s):%s\n",path,IMG_GetError());return NULL;}
    SDL_Texture *t=SDL_CreateTextureFromSurface(r,s);
    SDL_FreeSurface(s);return t;
}
static SDL_Texture *text_tex(SDL_Renderer *r,TTF_Font *f,
                              const char *txt,SDL_Color c,int *w,int *h){
    SDL_Surface *s=TTF_RenderUTF8_Blended(f,txt,c);
    if(!s)return NULL;
    SDL_Texture *t=SDL_CreateTextureFromSurface(r,s);
    if(w)*w=s->w;if(h)*h=s->h;
    SDL_FreeSurface(s);return t;
}
static void draw_center(SDL_Renderer *r,TTF_Font *f,const char *txt,int y,int win_w){
    SDL_Color c={255,255,255,255};int tw=0,th=0;
    SDL_Texture *t=text_tex(r,f,txt,c,&tw,&th);
    if(t){SDL_Rect d={(win_w-tw)/2,y,tw,th};SDL_RenderCopy(r,t,NULL,&d);SDL_DestroyTexture(t);}
}
static void draw_center_color(SDL_Renderer *r,TTF_Font *f,const char *txt,
                               int y,int win_w,SDL_Color c){
    int tw=0,th=0;
    SDL_Texture *t=text_tex(r,f,txt,c,&tw,&th);
    if(t){SDL_Rect d={(win_w-tw)/2,y,tw,th};SDL_RenderCopy(r,t,NULL,&d);SDL_DestroyTexture(t);}
}
static void draw_panel(SDL_Renderer *r,SDL_Rect rect){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,10,20,30,140);SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,180,240,255,220);SDL_RenderDrawRect(r,&rect);
}
static void draw_button(SDL_Renderer *r,TTF_Font *f,const Button *b){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    if(b->hovered)SDL_SetRenderDrawColor(r,40,140,200,190);
    else          SDL_SetRenderDrawColor(r,20,50,80,160);
    SDL_RenderFillRect(r,&b->rect);
    if(b->hovered)SDL_SetRenderDrawColor(r,220,250,255,255);
    else          SDL_SetRenderDrawColor(r,140,210,240,220);
    SDL_RenderDrawRect(r,&b->rect);
    SDL_Rect inner={b->rect.x+2,b->rect.y+2,b->rect.w-4,b->rect.h-4};
    SDL_SetRenderDrawColor(r,90,160,190,180);SDL_RenderDrawRect(r,&inner);
    SDL_Color tc={240,250,255,255};int tw=0,th=0;
    SDL_Texture *t=text_tex(r,f,b->label,tc,&tw,&th);
    if(t){SDL_Rect d={b->rect.x+(b->rect.w-tw)/2,b->rect.y+(b->rect.h-th)/2,tw,th};
          SDL_RenderCopy(r,t,NULL,&d);SDL_DestroyTexture(t);}
}
static void draw_level_button(SDL_Renderer *r,TTF_Font *f,TTF_Font *fs,
                               const Button *b,SDL_Color col,const char *desc,int hovered){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,col.r/3,col.g/3,col.b/3,hovered?220:160);
    SDL_RenderFillRect(r,&b->rect);
    SDL_SetRenderDrawColor(r,col.r,col.g,col.b,220);
    SDL_RenderDrawRect(r,&b->rect);
    if(hovered){
        SDL_Rect inner={b->rect.x+2,b->rect.y+2,b->rect.w-4,b->rect.h-4};
        SDL_SetRenderDrawColor(r,col.r,col.g,col.b,120);SDL_RenderDrawRect(r,&inner);
    }
    SDL_Color tc={240,250,255,255};int tw=0,th=0;
    SDL_Texture *t=text_tex(r,f,b->label,tc,&tw,&th);
    if(t){SDL_Rect d={b->rect.x+(b->rect.w-tw)/2,b->rect.y+12,tw,th};
          SDL_RenderCopy(r,t,NULL,&d);SDL_DestroyTexture(t);}
    SDL_Color dc={200,220,200,200};int dw=0,dh=0;
    SDL_Texture *dt=text_tex(r,fs,desc,dc,&dw,&dh);
    if(dt){SDL_Rect d={b->rect.x+(b->rect.w-dw)/2,b->rect.y+b->rect.h-dh-10,dw,dh};
           SDL_RenderCopy(r,dt,NULL,&d);SDL_DestroyTexture(dt);}
}
static void imgbtn_render(SDL_Renderer *r,const ImgButton *b){
    SDL_Texture *t=b->is_hover?b->hover:b->normal;
    if(t)SDL_RenderCopy(r,t,NULL,&b->rect);
}
static void imgbtn_destroy(ImgButton *b){
    if(b->normal){SDL_DestroyTexture(b->normal);b->normal=NULL;}
    if(b->hover) {SDL_DestroyTexture(b->hover); b->hover=NULL;}
}

static void draw_vies_et_serie(SDL_Renderer *r, TTF_Font *f,
                                int vies, int max_vies,
                                int streak, int cx, int y){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);

    int heart_w=16, heart_gap=4;
    int per_line = (max_vies<=6) ? max_vies : (max_vies+1)/2;
    int lines    = (max_vies+per_line-1)/per_line;
    int total_w  = per_line*(heart_w+heart_gap)-heart_gap;
    int start_x  = cx - total_w/2;

    for(int i=0;i<max_vies;i++){
        int line = i/per_line;
        int col  = i%per_line;
        int x    = start_x + col*(heart_w+heart_gap);
        int yy   = y + line*22;

        if(i<vies) SDL_SetRenderDrawColor(r,220,50,80,240);
        else       SDL_SetRenderDrawColor(r,80,40,50,120);

        SDL_Rect tl={x,    yy+2,  8, 6};
        SDL_Rect tr={x+9,  yy+2,  8, 6};
        SDL_Rect bot={x-1, yy+6, 18, 7};
        SDL_Rect tip={x+2, yy+11,14, 5};
        SDL_Rect pt ={x+5, yy+15, 8, 4};
        SDL_RenderFillRect(r,&tl);
        SDL_RenderFillRect(r,&tr);
        SDL_RenderFillRect(r,&bot);
        SDL_RenderFillRect(r,&tip);
        SDL_RenderFillRect(r,&pt);
    }

    if(streak>=2 && f){
        char str_txt[32];
        snprintf(str_txt,sizeof(str_txt),"Serie: %d",streak);
        int y_serie = y + lines*22 + 4;
        SDL_Color sc={255,140,0,220};
        int tw=0,th=0;
        SDL_Texture *t=text_tex(r,f,str_txt,sc,&tw,&th);
        if(t){
            SDL_Rect d={cx-tw/2, y_serie, tw, th};
            SDL_RenderCopy(r,t,NULL,&d);
            SDL_DestroyTexture(t);
        }
    }
}

static void draw_answer_animated(SDL_Renderer *r,TTF_Font *f,const Button *b,
                                  int isSelected,int isCorrect,
                                  int animActive,Uint32 animStart,Uint32 animDur){
    SDL_Rect rect=b->rect;
    float t=0.0f;
    if(animActive){Uint32 dt=SDL_GetTicks()-animStart;if(dt>animDur)dt=animDur;t=(float)dt/(float)animDur;}
    Uint8 bgR=b->hovered?40:20,bgG=b->hovered?140:50;
    Uint8 bgB=b->hovered?200:80,bgA=b->hovered?190:160;
    int shakeX=0,pulse=0;
    if(isSelected&&animActive){
        if(isCorrect){float p=(t<0.5f)?(t/0.5f):((1.0f-t)/0.5f);pulse=(int)(6.0f*p);bgR=30;bgG=170;bgB=90;bgA=210;}
        else{shakeX=(int)(8.0f*sinf(t*6.0f*3.14159f*2.0f));bgR=170;bgG=60;bgB=60;bgA=210;}
    }
    rect.x+=shakeX-pulse;rect.y-=pulse;rect.w+=pulse*2;rect.h+=pulse*2;
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,bgR,bgG,bgB,bgA);SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,180,240,255,220);SDL_RenderDrawRect(r,&rect);
    SDL_Rect inner={rect.x+2,rect.y+2,rect.w-4,rect.h-4};
    SDL_SetRenderDrawColor(r,90,160,190,180);SDL_RenderDrawRect(r,&inner);
    SDL_Color tc={240,250,255,255};int tw=0,th=0;
    SDL_Texture *ttex=text_tex(r,f,b->label,tc,&tw,&th);
    if(ttex){SDL_Rect d={rect.x+20,rect.y+(rect.h-th)/2,tw,th};
             SDL_RenderCopy(r,ttex,NULL,&d);SDL_DestroyTexture(ttex);}
}

static void draw_chrono_bar(SDL_Renderer *r,int cx,int y,int total_w,int bar_h,
                             Uint32 chrono_start,Uint32 duration_ms,
                             Uint32 bonus_flash_start,int bonus_flash_active){
    Uint32 elapsed=SDL_GetTicks()-chrono_start;
    float ratio=1.0f-(float)elapsed/(float)duration_ms;
    if(ratio<0.0f)ratio=0.0f;
    SDL_Rect track={cx-total_w/2,y,total_w,bar_h};
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,15,25,45,160);SDL_RenderFillRect(r,&track);
    SDL_SetRenderDrawColor(r,60,100,140,180);SDL_RenderDrawRect(r,&track);
    int fill_w=(int)(total_w*ratio);
    if(fill_w>0){
        int is_gold=bonus_flash_active&&(SDL_GetTicks()-bonus_flash_start<600);
        Uint8 cr,cg,cb=0;
        if(is_gold){float p=(sinf((float)SDL_GetTicks()*0.025f)+1.0f)*0.5f;cr=255;cg=(Uint8)(180+p*75);}
        else if(ratio>0.5f){cr=(Uint8)((1.0f-ratio)*2.0f*230.0f);cg=210;}
        else{cr=210;cg=(Uint8)(ratio*2.0f*190.0f);}
        Uint8 alpha=220;
        if(!is_gold&&ratio<0.3f){float p=(sinf((float)SDL_GetTicks()*0.014f)+1.0f)*0.5f;alpha=(Uint8)(140+p*115);}
        SDL_Rect fill={cx-total_w/2,y,fill_w,bar_h};
        SDL_SetRenderDrawColor(r,cr,cg,cb,alpha);SDL_RenderFillRect(r,&fill);
        if(is_gold){SDL_SetRenderDrawColor(r,255,255,200,180);SDL_RenderDrawLine(r,cx-total_w/2,y,cx-total_w/2+fill_w,y);}
    }
}

static void draw_rotozoom(SDL_Renderer *r,SDL_Texture *tex,int tw,int th,
                           int win_w,int win_h,Uint32 start,Uint32 duration_ms){
    if(!tex)return;
    Uint32 elapsed=SDL_GetTicks()-start;
    if(elapsed>duration_ms)elapsed=duration_ms;
    float t=(float)elapsed/(float)duration_ms;
    float scale;
    if     (t<0.40f)scale=(t/0.40f)*1.35f;
    else if(t<0.65f)scale=1.35f-((t-0.40f)/0.25f)*0.42f;
    else            scale=0.93f+((t-0.65f)/0.35f)*0.07f;
    float angle=sinf(t*3.14159f*2.8f)*20.0f*(1.0f-t);
    int dw=(int)(tw*scale),dh=(int)(th*scale);
    SDL_Rect dst={win_w/2-dw/2,win_h/2-dh/2,dw,dh};
    SDL_RenderCopyEx(r,tex,NULL,&dst,(double)angle,NULL,SDL_FLIP_NONE);
}

static void draw_star(SDL_Renderer *r,int cx,int cy,int radius,int filled){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    if(filled)SDL_SetRenderDrawColor(r,255,215,0,255);
    else      SDL_SetRenderDrawColor(r,80,80,80,180);
    float pi=3.14159f,outer=(float)radius,inner_r=outer*0.4f;
    SDL_Point pts[11];
    for(int i=0;i<10;i++){
        float angle=-pi/2.0f+i*pi/5.0f;
        float rad=(i%2==0)?outer:inner_r;
        pts[i].x=cx+(int)(cosf(angle)*rad);
        pts[i].y=cy+(int)(sinf(angle)*rad);
    }
    pts[10]=pts[0];
    for(int i=0;i<10;i++)SDL_RenderDrawLine(r,pts[i].x,pts[i].y,pts[i+1].x,pts[i+1].y);
    if(filled){
        for(int dy=-radius;dy<=radius;dy++){
            int py=cy+dy,x1=cx-radius,x2=cx+radius,hit=0;
            for(int dx=-radius;dx<=radius;dx++){
                int px=cx+dx,in2=0;
                for(int ii=0,jj=9;ii<10;jj=ii++){
                    if(((pts[ii].y>py)!=(pts[jj].y>py))&&
                       (pts[ii].x+(float)(py-pts[ii].y)/
                       (pts[jj].y-pts[ii].y)*(pts[jj].x-pts[ii].x)<(float)px))
                        in2=!in2;
                }
                if(in2){if(!hit){x1=px;hit=1;}x2=px;}
            }
            if(hit)SDL_RenderDrawLine(r,x1,py,x2,py);
        }
    }
}

static void draw_results_screen(SDL_Renderer *r,TTF_Font *title_f,TTF_Font *small_f,
                                  int score,int nb_q,int correct_count,
                                  int vies_restantes,
                                  int W,int H,int streak_max,
                                  int level_idx,Uint32 enter_time){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,0,10,25,210);
    SDL_Rect bg={0,0,W,H};SDL_RenderFillRect(r,&bg);

    float t_in=(float)(SDL_GetTicks()-enter_time)/600.0f;
    if(t_in>1.0f)t_in=1.0f;
    draw_center_color(r,title_f,"RESULTATS",(int)(20+(1.0f-t_in)*(-60)),W,(SDL_Color){255,220,80,255});

    char lv_txt[64];snprintf(lv_txt,sizeof(lv_txt),"Niveau: %s",LEVELS[level_idx].name);
    draw_center_color(r,small_f,lv_txt,72,W,LEVELS[level_idx].color);

    char sc_txt[64];snprintf(sc_txt,sizeof(sc_txt),"Score Final: %d pts",score);
    draw_center_color(r,small_f,sc_txt,102,W,(SDL_Color){200,255,200,255});

    char q_txt[64];snprintf(q_txt,sizeof(q_txt),"Bonnes reponses: %d / %d",correct_count,nb_q);
    draw_center_color(r,small_f,q_txt,132,W,(SDL_Color){180,220,255,220});

    char str_txt[64];snprintf(str_txt,sizeof(str_txt),"Meilleure serie: %d",streak_max);
    draw_center_color(r,small_f,str_txt,160,W,(SDL_Color){255,180,100,220});

    int vies_perdues = MAX_VIES - vies_restantes;
    int nb_stars;
    if     (vies_perdues == 0)              nb_stars=3;
    else if(vies_perdues <= 2)              nb_stars=2;
    else if(vies_perdues <= MAX_VIES-1)     nb_stars=1;
    else                                    nb_stars=0;

    int star_r=45,star_y=245,spacing=130,start_x=W/2-spacing;
    for(int i=0;i<3;i++){
        Uint32 star_delay=(Uint32)(i*300);
        int visible=(SDL_GetTicks()-enter_time)>(600+star_delay);
        draw_star(r,start_x+i*spacing,star_y,star_r,visible&&(i<nb_stars));
    }

    const char *msg="";SDL_Color msg_col={255,255,255,255};
    if     (nb_stars==3){msg="Excellent !  Parfait !"; msg_col=(SDL_Color){255,215,0,255};}
    else if(nb_stars==2){msg="Bien joue !";            msg_col=(SDL_Color){100,255,160,255};}
    else if(nb_stars==1){msg="Pas mal, continue !";    msg_col=(SDL_Color){255,160,80,255};}
    else                {msg="Essaie encore !";         msg_col=(SDL_Color){255,80,80,255};}
    draw_center_color(r,small_f,msg,315,W,msg_col);

    Uint8 alpha_pulse=(Uint8)(180+60*sinf((float)SDL_GetTicks()*0.004f));
    draw_center_color(r,small_f,"Appuyez sur RETOUR pour rejouer",H-80,W,(SDL_Color){200,220,255,alpha_pulse});
}

static void draw_gameover_screen(SDL_Renderer *r,TTF_Font *title_f,TTF_Font *small_f,
                                  int score,int correct_count,int nb_q,
                                  int W,int H,Uint32 enter_time){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,20,0,0,220);
    SDL_Rect bg={0,0,W,H};SDL_RenderFillRect(r,&bg);
    float t_in=(float)(SDL_GetTicks()-enter_time)/800.0f;
    if(t_in>1.0f)t_in=1.0f;
    float scale=0.5f+t_in*0.5f;
    int tw=0,th=0;
    SDL_Texture *ttex=text_tex(r,title_f,"GAME OVER",(SDL_Color){220,40,40,255},&tw,&th);
    if(ttex){int dw=(int)(tw*scale),dh=(int)(th*scale);
             SDL_Rect dst={W/2-dw/2,60,dw,dh};SDL_RenderCopy(r,ttex,NULL,&dst);SDL_DestroyTexture(ttex);}
    SDL_SetRenderDrawColor(r,180,30,30,200);
    SDL_RenderDrawLine(r,W/2-200,130,W/2+200,130);
    char sc_txt[64];snprintf(sc_txt,sizeof(sc_txt),"Score: %d pts",score);
    draw_center_color(r,small_f,sc_txt,155,W,(SDL_Color){255,180,180,255});
    char q_txt[64];snprintf(q_txt,sizeof(q_txt),"Reponses correctes: %d / %d",correct_count,nb_q);
    draw_center_color(r,small_f,q_txt,190,W,(SDL_Color){220,160,160,220});
    draw_center_color(r,small_f,"Toutes vos vies sont epuisees !",240,W,(SDL_Color){255,120,120,200});
    draw_center_color(r,small_f,"Recommencez et faites mieux cette fois !",275,W,(SDL_Color){200,100,100,180});

    int hearts_cx=W/2,heart_w=16;
    int total_w2=MAX_VIES*heart_w+(MAX_VIES-1)*4;
    int hx=hearts_cx-total_w2/2;
    for(int i=0;i<MAX_VIES;i++){
        int x=hx+i*(heart_w+4);
        SDL_SetRenderDrawColor(r,80,20,20,160);
        SDL_Rect tl={x,330,8,6},tr={x+9,330,8,6};
        SDL_Rect bot={x-1,334,18,7},tip={x+2,339,14,5},pt={x+5,343,8,4};
        SDL_RenderFillRect(r,&tl);SDL_RenderFillRect(r,&tr);
        SDL_RenderFillRect(r,&bot);SDL_RenderFillRect(r,&tip);SDL_RenderFillRect(r,&pt);
    }
    Uint8 alpha_pulse=(Uint8)(160+80*sinf((float)SDL_GetTicks()*0.005f));
    draw_center_color(r,small_f,"Appuyez sur RETOUR pour reessayer",H-80,W,(SDL_Color){255,160,160,alpha_pulse});
}

static void shuffle_indices(int *arr,int n){
    for(int i=n-1;i>0;--i){int j=rand()%(i+1),tmp=arr[i];arr[i]=arr[j];arr[j]=tmp;}
}
static void set_question_labels(Button *a,Button *b,Button *c,int qi){
    strcpy(a->label,quizList[qi].answers[0]);
    strcpy(b->label,quizList[qi].answers[1]);
    strcpy(c->label,quizList[qi].answers[2]);
}
static void pending_rotate_skip(int *p,int n){
    if(n<=1)return;int first=p[0];
    for(int i=0;i<n-1;i++)p[i]=p[i+1];p[n-1]=first;
}
static void pending_pop(int *p,int *n){
    if(*n<=0)return;
    for(int i=0;i<(*n)-1;i++)p[i]=p[i+1];(*n)--;
}

int enigme_run(SDL_Window *window, SDL_Renderer *renderer){
    (void)window;
    int W=0,H=0;
    SDL_RenderGetLogicalSize(renderer,&W,&H);
    if(W<=0||H<=0)SDL_GetRendererOutputSize(renderer,&W,&H);
    if(W<=0||H<=0){W=1000;H=600;}
    srand((unsigned)time(NULL));
    NB_QUESTIONS=load_questions(QUESTIONS_FILE);
    if(NB_QUESTIONS<1){fprintf(stderr,"[ENIGME] Aucune question.\n");return 1;}
    if(!(IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG))fprintf(stderr,"IMG_Init:%s\n",IMG_GetError());
    if(TTF_Init()!=0){fprintf(stderr,"TTF_Init:%s\n",TTF_GetError());return 1;}
    int audio_ok=1;
    if(Mix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,2048)<0){fprintf(stderr,"Mix_OpenAudio:%s\n",Mix_GetError());audio_ok=0;}

    SDL_Texture *bg_menu  =load_texture(renderer,BG_MENU_PATH);
    SDL_Texture *bg_quiz  =load_texture(renderer,BG_QUIZ_PATH);
    TTF_Font *title_font  =TTF_OpenFont(FONT_PATH,FONT_TITLE);
    TTF_Font *small_font  =TTF_OpenFont(FONT_PATH,FONT_SMALL);
    TTF_Font *big_font    =TTF_OpenFont(FONT_PATH,FONT_BIG);
    if(!title_font||!small_font||!big_font){
        fprintf(stderr,"Erreur ouverture police.\n");
        if(bg_menu)SDL_DestroyTexture(bg_menu);if(bg_quiz)SDL_DestroyTexture(bg_quiz);
        if(title_font)TTF_CloseFont(title_font);if(small_font)TTF_CloseFont(small_font);
        if(big_font)TTF_CloseFont(big_font);if(audio_ok)Mix_CloseAudio();
        TTF_Quit();IMG_Quit();return 1;
    }
    Mix_Music *quizMusic=NULL;
    if(audio_ok){quizMusic=Mix_LoadMUS(MUSIC_QUIZ);if(!quizMusic)fprintf(stderr,"LoadMUS:%s\n",Mix_GetError());}

    Button btnQuiz={.rect={W/2-200,H/2-70,400,70},.hovered=0};
    Button btnPuzzle={.rect={W/2-200,H/2+20,400,70},.hovered=0};
    strcpy(btnQuiz.label,"QUIZ");strcpy(btnPuzzle.label,"PUZZLE");
    ImgButton menuRetour=(ImgButton){0},menuQuitter=(ImgButton){0};
    menuRetour.rect=(SDL_Rect){40,H-85,220,55};menuQuitter.rect=(SDL_Rect){W-260,H-85,220,55};
    menuRetour.normal=load_texture(renderer,"retour1.png");menuRetour.hover=load_texture(renderer,"hretour.png");
    menuQuitter.normal=load_texture(renderer,"quitter.png");menuQuitter.hover=load_texture(renderer,"hquitter.png");

    Button btnLevels[NB_LEVELS];
    int btn_w=560,btn_h=90,btn_gap=20;
    int total_h=NB_LEVELS*(btn_h+btn_gap)-btn_gap;
    int start_y=H/2-total_h/2+20;
    for(int i=0;i<NB_LEVELS;i++){
        btnLevels[i].rect=(SDL_Rect){W/2-btn_w/2,start_y+i*(btn_h+btn_gap),btn_w,btn_h};
        btnLevels[i].hovered=0;strcpy(btnLevels[i].label,LEVELS[i].name);
    }
    ImgButton diffRetour=(ImgButton){0};
    diffRetour.rect=(SDL_Rect){40,H-85,220,55};
    diffRetour.normal=load_texture(renderer,"retour1.png");diffRetour.hover=load_texture(renderer,"hretour.png");

    ImgButton quizRetour=(ImgButton){0},quizQuitter=(ImgButton){0};
    quizRetour.rect=(SDL_Rect){40,H-85,220,55};quizQuitter.rect=(SDL_Rect){W-260,H-85,220,55};
    quizRetour.normal=load_texture(renderer,"retour1.png");quizRetour.hover=load_texture(renderer,"hretour.png");
    quizQuitter.normal=load_texture(renderer,"quitter.png");quizQuitter.hover=load_texture(renderer,"hquitter.png");

    ImgButton resRetour=(ImgButton){0},goRetour=(ImgButton){0};
    resRetour.rect=(SDL_Rect){W/2-110,H-85,220,55};
    resRetour.normal=load_texture(renderer,"retour1.png");resRetour.hover=load_texture(renderer,"hretour.png");
    goRetour.rect=(SDL_Rect){W/2-110,H-85,220,55};
    goRetour.normal=load_texture(renderer,"retour1.png");goRetour.hover=load_texture(renderer,"hretour.png");

    Button ansA={.rect={W/2-320,215,640,58},.hovered=0};
    Button ansB={.rect={W/2-320,290,640,58},.hovered=0};
    Button ansC={.rect={W/2-320,365,640,58},.hovered=0};
    Button btnSkip={.rect={W/2-110,445,220,50},.hovered=0};
    strcpy(btnSkip.label,"SKIP");

    int score=0,correct_count=0,vies=MAX_VIES;
    int pending[MAX_QUESTIONS];
    int pending_n=0,currentQ=0,question_num=0;
    int answered=0,selected_choice=-1,is_correct=0;
    char result_msg[128]="";
    int anim_active=0;Uint32 anim_start=0;
    const Uint32 anim_dur=500;
    int feedback_active=0;Uint32 feedback_start=0,chrono_start=0;
    int streak=0,streak_max=0;
    char bonus_msg[64]="";Uint32 bonus_msg_t=0;
    const Uint32 BONUS_MSG_MS=1200;
    Uint32 bonus_flash_start=0;int bonus_flash_active=0;
    Uint32 chrono_bonus_ms=0;
    SDL_Texture *rtz_tex=NULL;int rtz_w=0,rtz_h=0;
    Uint32 rtz_start=0;int rtz_active=0;
    int level_idx=1;
    Uint32 current_chrono_ms=LEVELS[1].chrono_ms;
    int current_nb_q=LEVELS[1].nb_q;
    Uint32 results_enter_t=0,gameover_enter_t=0;

    displayed_score=0;target_score=0;score_anim_t=0;
    for(int i=0;i<MAX_PARTICLES;i++)particles[i].active=0;

    State st=ST_DIFFICULTY;
    int running=1,quit_game=0,music_started=0,ignore_click=0;

    SDL_Event ev;
    while(running){
        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT){quit_game=1;running=0;}
            if(ev.type==SDL_MOUSEBUTTONUP&&ev.button.button==SDL_BUTTON_LEFT)ignore_click=0;
            if(ev.type==SDL_KEYDOWN){
                if(ev.key.keysym.sym==SDLK_ESCAPE){quit_game=1;running=0;}
                if(st==ST_QUIZ&&ev.key.keysym.sym==SDLK_BACKSPACE){
                    st=ST_DIFFICULTY;music_started=0;
                    answered=0;result_msg[0]='\0';selected_choice=-1;
                    anim_active=0;feedback_active=0;rtz_active=0;
                    for(int i=0;i<MAX_PARTICLES;i++)particles[i].active=0;
                    if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                    if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();
                }
                if(st==ST_DIFFICULTY&&ev.key.keysym.sym==SDLK_BACKSPACE){quit_game=0;running=0;}
            }
            if(ev.type==SDL_MOUSEMOTION){
                int mx=ev.motion.x,my=ev.motion.y;
                if(st==ST_MENU){btnQuiz.hovered=in_rect(mx,my,&btnQuiz.rect);btnPuzzle.hovered=in_rect(mx,my,&btnPuzzle.rect);menuRetour.is_hover=in_rect(mx,my,&menuRetour.rect);menuQuitter.is_hover=in_rect(mx,my,&menuQuitter.rect);}
                else if(st==ST_DIFFICULTY){for(int i=0;i<NB_LEVELS;i++)btnLevels[i].hovered=in_rect(mx,my,&btnLevels[i].rect);diffRetour.is_hover=in_rect(mx,my,&diffRetour.rect);}
                else if(st==ST_QUIZ){quizRetour.is_hover=in_rect(mx,my,&quizRetour.rect);quizQuitter.is_hover=in_rect(mx,my,&quizQuitter.rect);ansA.hovered=in_rect(mx,my,&ansA.rect);ansB.hovered=in_rect(mx,my,&ansB.rect);ansC.hovered=in_rect(mx,my,&ansC.rect);btnSkip.hovered=(!answered)?in_rect(mx,my,&btnSkip.rect):0;}
                else if(st==ST_RESULTS)resRetour.is_hover=in_rect(mx,my,&resRetour.rect);
                else if(st==ST_GAMEOVER)goRetour.is_hover=in_rect(mx,my,&goRetour.rect);
            }
            if(ev.type==SDL_MOUSEBUTTONDOWN&&ev.button.button==SDL_BUTTON_LEFT){
                int mx=ev.button.x,my=ev.button.y;
                if(st==ST_MENU){
                    if(in_rect(mx,my,&menuRetour.rect)){quit_game=0;running=0;}
                    else if(in_rect(mx,my,&menuQuitter.rect)){quit_game=1;running=0;}
                    else if(in_rect(mx,my,&btnQuiz.rect))st=ST_DIFFICULTY;
                }
                else if(st==ST_DIFFICULTY){
                    if(in_rect(mx,my,&diffRetour.rect)){quit_game=0;running=0;}
                    else for(int i=0;i<NB_LEVELS;i++){
                        if(in_rect(mx,my,&btnLevels[i].rect)){
                            level_idx=i;current_chrono_ms=LEVELS[i].chrono_ms;current_nb_q=LEVELS[i].nb_q;
                            if(current_nb_q>NB_QUESTIONS)current_nb_q=NB_QUESTIONS;
                            st=ST_QUIZ;ignore_click=1;
                            for(int j=0;j<NB_QUESTIONS;j++)pending[j]=j;
                            shuffle_indices(pending,NB_QUESTIONS);
                            pending_n=current_nb_q;currentQ=pending[0];question_num=1;
                            set_question_labels(&ansA,&ansB,&ansC,currentQ);
                            score=0;correct_count=0;vies=MAX_VIES;
                            answered=0;result_msg[0]='\0';selected_choice=-1;
                            anim_active=0;feedback_active=0;rtz_active=0;
                            streak=0;streak_max=0;bonus_msg[0]='\0';bonus_flash_active=0;chrono_bonus_ms=0;
                            displayed_score=0;target_score=0;
                            for(int j=0;j<MAX_PARTICLES;j++)particles[j].active=0;
                            if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                            chrono_start=SDL_GetTicks();
                            if(audio_ok&&quizMusic&&!music_started){Mix_HaltMusic();Mix_PlayMusic(quizMusic,-1);music_started=1;}
                            break;
                        }
                    }
                }
                else if(st==ST_QUIZ){
                    if(ignore_click){ignore_click=0;continue;}
                    if(in_rect(mx,my,&quizRetour.rect)){
                        st=ST_DIFFICULTY;music_started=0;answered=0;result_msg[0]='\0';selected_choice=-1;
                        anim_active=0;feedback_active=0;rtz_active=0;
                        for(int i=0;i<MAX_PARTICLES;i++)particles[i].active=0;
                        if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                        if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();
                    } else if(in_rect(mx,my,&quizQuitter.rect)){quit_game=1;running=0;}
                    else if(!answered&&in_rect(mx,my,&btnSkip.rect)){
                        vies--;pending_rotate_skip(pending,pending_n);currentQ=pending[0];
                        set_question_labels(&ansA,&ansB,&ansC,currentQ);
                        answered=0;result_msg[0]='\0';selected_choice=-1;
                        anim_active=0;feedback_active=0;streak=0;bonus_flash_active=0;chrono_bonus_ms=0;
                        chrono_start=SDL_GetTicks();
                        if(vies<=0){st=ST_GAMEOVER;gameover_enter_t=SDL_GetTicks();if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();music_started=0;}
                    } else if(!answered){
                        int choice=-1;
                        if(in_rect(mx,my,&ansA.rect))choice=0;
                        else if(in_rect(mx,my,&ansB.rect))choice=1;
                        else if(in_rect(mx,my,&ansC.rect))choice=2;
                        if(choice!=-1){
                            answered=1;selected_choice=choice;
                            is_correct=(choice==quizList[currentQ].correct);
                            anim_active=1;anim_start=SDL_GetTicks();
                            if(is_correct){
                                int pts=POINTS_BASE;bonus_msg[0]='\0';bonus_flash_active=0;
                                Uint32 el=SDL_GetTicks()-chrono_start;
                                if(el<5000){pts+=POINTS_SPEED;chrono_bonus_ms=BONUS_TEMPS_MS;bonus_flash_start=SDL_GetTicks();bonus_flash_active=1;snprintf(bonus_msg,sizeof(bonus_msg),"+%d pts  +1 sec !",POINTS_SPEED);bonus_msg_t=SDL_GetTicks();}
                                streak++;if(streak>streak_max)streak_max=streak;
                                if(streak>=STREAK_NEEDED&&streak%STREAK_NEEDED==0){pts+=POINTS_STREAK;snprintf(bonus_msg,sizeof(bonus_msg),"+%d Bonus Serie x%d !",POINTS_STREAK,streak);bonus_msg_t=SDL_GetTicks();}
                                correct_count++;score+=pts;target_score=score;score_anim_t=SDL_GetTicks();
                                snprintf(result_msg,sizeof(result_msg),"Correct !  +%d pts",pts);
                                particles_spawn(W,H);
                            } else {
                                streak=0;bonus_flash_active=0;chrono_bonus_ms=0;vies--;
                                snprintf(result_msg,sizeof(result_msg),"Faux !  Reponse: %c",(char)('A'+quizList[currentQ].correct));
                            }
                            if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                            SDL_Color rtz_col=is_correct?(SDL_Color){80,255,130,255}:(SDL_Color){255,70,70,255};
                            rtz_tex=text_tex(renderer,title_font,result_msg,rtz_col,&rtz_w,&rtz_h);
                            rtz_start=SDL_GetTicks();rtz_active=1;feedback_active=1;feedback_start=SDL_GetTicks();
                        }
                    }
                }
                else if(st==ST_RESULTS){
                    if(in_rect(mx,my,&resRetour.rect)){if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();music_started=0;quit_game=0;running=0;}
                }
                else if(st==ST_GAMEOVER){
                    if(in_rect(mx,my,&goRetour.rect)){if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();music_started=0;quit_game=0;running=0;}
                }
            }
        }

        if(st==ST_QUIZ&&!answered){
            Uint32 total_dur=current_chrono_ms+chrono_bonus_ms;
            if(SDL_GetTicks()-chrono_start>=total_dur){
                answered=1;selected_choice=-1;is_correct=0;streak=0;bonus_flash_active=0;chrono_bonus_ms=0;vies--;
                snprintf(result_msg,sizeof(result_msg),"Temps ecoule !  Reponse: %c",(char)('A'+quizList[currentQ].correct));
                if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                SDL_Color rtz_col={255,160,0,255};
                rtz_tex=text_tex(renderer,title_font,"Temps ecoule !",rtz_col,&rtz_w,&rtz_h);
                rtz_start=SDL_GetTicks();rtz_active=1;feedback_active=1;feedback_start=SDL_GetTicks();
            }
        }

        if(anim_active&&SDL_GetTicks()-anim_start>=anim_dur)anim_active=0;
        if(rtz_active&&SDL_GetTicks()-rtz_start>=RTZ_MS)rtz_active=0;
        if(displayed_score<target_score){if(SDL_GetTicks()-score_anim_t>=60){displayed_score++;score_anim_t=SDL_GetTicks();}}
        if(st==ST_QUIZ)particles_update(H);

        if(st==ST_QUIZ&&feedback_active&&SDL_GetTicks()-feedback_start>=FEEDBACK_MS){
            feedback_active=0;
            if(vies<=0){
                st=ST_GAMEOVER;gameover_enter_t=SDL_GetTicks();
                if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();music_started=0;rtz_active=0;
                for(int i=0;i<MAX_PARTICLES;i++)particles[i].active=0;
                if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
            } else {
                pending_pop(pending,&pending_n);
                if(pending_n<=0){
                    st=ST_RESULTS;results_enter_t=SDL_GetTicks();
                    if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();music_started=0;rtz_active=0;
                    for(int i=0;i<MAX_PARTICLES;i++)particles[i].active=0;
                    if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                } else {
                    currentQ=pending[0];question_num++;
                    set_question_labels(&ansA,&ansB,&ansC,currentQ);
                    answered=0;result_msg[0]='\0';selected_choice=-1;
                    anim_active=0;rtz_active=0;bonus_flash_active=0;
                    if(rtz_tex){SDL_DestroyTexture(rtz_tex);rtz_tex=NULL;}
                    chrono_start=SDL_GetTicks();
                }
            }
        }

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);

        if(st==ST_MENU){
            if(bg_menu){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_menu,NULL,&d);}
            draw_button(renderer,small_font,&btnQuiz);
            draw_button(renderer,small_font,&btnPuzzle);
            imgbtn_render(renderer,&menuRetour);
            imgbtn_render(renderer,&menuQuitter);
        }
        else if(st==ST_DIFFICULTY){
            if(bg_menu){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_menu,NULL,&d);}
            SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer,0,10,25,160);
            SDL_Rect ov={W/2-320,60,640,H-130};SDL_RenderFillRect(renderer,&ov);
            draw_center(renderer,title_font,"CHOISIR NIVEAU",75,W);
            for(int i=0;i<NB_LEVELS;i++)
                draw_level_button(renderer,title_font,small_font,&btnLevels[i],LEVELS[i].color,LEVELS[i].desc,btnLevels[i].hovered);
            imgbtn_render(renderer,&diffRetour);
        }
        else if(st==ST_QUIZ){
            if(bg_quiz){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_quiz,NULL,&d);}

            SDL_Color lc=LEVELS[level_idx].color;
            SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer,lc.r/4,lc.g/4,lc.b/4,160);
            SDL_Rect badge={8,8,110,28};SDL_RenderFillRect(renderer,&badge);
            SDL_SetRenderDrawColor(renderer,lc.r,lc.g,lc.b,200);SDL_RenderDrawRect(renderer,&badge);
            int bw=0,bh=0;
            SDL_Texture *bt=text_tex(renderer,small_font,LEVELS[level_idx].name,lc,&bw,&bh);
            if(bt){SDL_Rect d={badge.x+(badge.w-bw)/2,badge.y+(badge.h-bh)/2,bw,bh};SDL_RenderCopy(renderer,bt,NULL,&d);SDL_DestroyTexture(bt);}

            draw_vies_et_serie(renderer,small_font,vies,MAX_VIES,streak,63,42);

            draw_center(renderer,title_font,"QUIZ",10,W);

            char scoreTxt[64];snprintf(scoreTxt,sizeof(scoreTxt),"Score: %d",displayed_score);
            float score_scale=1.0f;Uint32 since_a=SDL_GetTicks()-score_anim_t;
            if(displayed_score<target_score||since_a<300){float p=1.0f-(float)since_a/300.0f;if(p<0)p=0;score_scale=1.0f+0.25f*p;}
            SDL_Color sc=(displayed_score<target_score)?(SDL_Color){120,255,160,255}:(SDL_Color){255,255,255,255};
            int stw=0,sth=0;
            SDL_Texture *st_tex=text_tex(renderer,small_font,scoreTxt,sc,&stw,&sth);
            if(st_tex){int dw=(int)(stw*score_scale),dh=(int)(sth*score_scale);
                       SDL_Rect sdst={W/2-dw/2,58-(dh-sth)/2,dw,dh};
                       SDL_RenderCopy(renderer,st_tex,NULL,&sdst);SDL_DestroyTexture(st_tex);}

            if(bonus_msg[0]&&SDL_GetTicks()-bonus_msg_t<BONUS_MSG_MS){
                float bt2=1.0f-(float)(SDL_GetTicks()-bonus_msg_t)/(float)BONUS_MSG_MS;
                int by=(int)(H/2-80-(1.0f-bt2)*40);
                draw_center_color(renderer,small_font,bonus_msg,by,W,(SDL_Color){255,230,0,(Uint8)(bt2*255)});
            }

            char qnum[64];snprintf(qnum,sizeof(qnum),"Question %d / %d",question_num,current_nb_q);
            SDL_Color gray={180,220,255,210};int qnw=0,qnh=0;
            SDL_Texture *qnt=text_tex(renderer,small_font,qnum,gray,&qnw,&qnh);
            if(qnt){SDL_Rect d={W-qnw-18,15,qnw,qnh};SDL_RenderCopy(renderer,qnt,NULL,&d);SDL_DestroyTexture(qnt);}

            SDL_Rect qpanel={W/2-360,93,720,98};draw_panel(renderer,qpanel);
            SDL_Color qc={240,250,255,255};int tw=0,th=0;
            SDL_Texture *qt=text_tex(renderer,small_font,quizList[currentQ].question,qc,&tw,&th);
            if(qt){SDL_Rect d={qpanel.x+(qpanel.w-tw)/2,qpanel.y+(qpanel.h-th)/2,tw,th};SDL_RenderCopy(renderer,qt,NULL,&d);SDL_DestroyTexture(qt);}

            if(!answered){
                Uint32 total_dur=current_chrono_ms+chrono_bonus_ms;
                draw_chrono_bar(renderer,W/2,198,718,11,chrono_start,total_dur,bonus_flash_start,bonus_flash_active);
            }

            draw_answer_animated(renderer,small_font,&ansA,selected_choice==0,is_correct,anim_active,anim_start,anim_dur);
            draw_answer_animated(renderer,small_font,&ansB,selected_choice==1,is_correct,anim_active,anim_start,anim_dur);
            draw_answer_animated(renderer,small_font,&ansC,selected_choice==2,is_correct,anim_active,anim_start,anim_dur);

            if(!answered)draw_button(renderer,small_font,&btnSkip);
            particles_draw(renderer);
            if(rtz_active&&rtz_tex)draw_rotozoom(renderer,rtz_tex,rtz_w,rtz_h,W,H,rtz_start,RTZ_MS);
            imgbtn_render(renderer,&quizRetour);
            imgbtn_render(renderer,&quizQuitter);
        }
        else if(st==ST_RESULTS){
            if(bg_menu){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_menu,NULL,&d);}
            draw_results_screen(renderer,title_font,small_font,
                                score,current_nb_q,correct_count,
                                vies,W,H,streak_max,level_idx,results_enter_t);
            imgbtn_render(renderer,&resRetour);
        }
        else if(st==ST_GAMEOVER){
            if(bg_quiz){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_quiz,NULL,&d);}
            draw_gameover_screen(renderer,title_font,small_font,score,correct_count,current_nb_q,W,H,gameover_enter_t);
            imgbtn_render(renderer,&goRetour);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if(audio_ok&&Mix_PlayingMusic())Mix_HaltMusic();
    if(quizMusic)Mix_FreeMusic(quizMusic);
    if(rtz_tex)SDL_DestroyTexture(rtz_tex);
    imgbtn_destroy(&menuRetour);imgbtn_destroy(&menuQuitter);
    imgbtn_destroy(&diffRetour);
    imgbtn_destroy(&quizRetour);imgbtn_destroy(&quizQuitter);
    imgbtn_destroy(&resRetour);imgbtn_destroy(&goRetour);
    if(bg_menu)SDL_DestroyTexture(bg_menu);
    if(bg_quiz)SDL_DestroyTexture(bg_quiz);
    TTF_CloseFont(title_font);TTF_CloseFont(small_font);TTF_CloseFont(big_font);
    if(audio_ok)Mix_CloseAudio();
    TTF_Quit();IMG_Quit();
    return quit_game?1:0;
}
