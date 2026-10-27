#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define BG_MENU_PATH   "photo1.png"
#define BG_QUIZ_PATH   "photo2.png"
#define MUSIC_QUIZ     "son.mp3"
#define FONT_PATH      "font.ttf"
#define FONT_TITLE     40
#define FONT_SMALL     24

typedef struct Button {
    SDL_Rect rect;
    char label[256];
    int hovered;
} Button;

typedef enum { ST_MENU=0, ST_QUIZ=1 } State;

typedef struct {
    SDL_Rect rect;
    SDL_Texture *normal;
    SDL_Texture *hover;
    int is_hover;
} ImgButton;

typedef struct {
    const char *question;
    const char *answers[3];
    int correct;
} QuizQ;

#define NB_QUESTIONS 15
static QuizQ quizList[NB_QUESTIONS] = {
    {"Quelle est la source d'energie du train ?",
     {"A) Le charbon eternel","B) Le moteur perpetuel","C) L'energie solaire"}, 1},
    {"Que represente le train dans Snowpiercer ?",
     {"A) Une prison mobile","B) Un espoir de survie","C) Une base militaire"}, 1},
    {"Ou se trouve le monde exterieur ?",
     {"A) En feu","B) Sous l'eau","C) Gele"}, 2},
    {"Combien de classes sociales dans le train ?",
     {"A) 2","B) 3","C) Plusieurs"}, 2},
    {"Le train s'arrete-t-il jamais ?",
     {"A) Oui","B) Non","C) Parfois"}, 1},
    {"Comment s'appelle le protagoniste principal ?",
     {"A) Curtis","B) Gilliam","C) Mason"}, 0},
    {"Ou vivent les plus pauvres dans le train ?",
     {"A) A l'avant","B) Au milieu","C) A l'arriere"}, 2},
    {"Quelle nourriture mangent les pauvres ?",
     {"A) Du pain","B) Des barres proteinees noires","C) Du riz"}, 1},
    {"Qui est le createur et proprietaire du train ?",
     {"A) Curtis","B) Wilford","C) Namgoong"}, 1},
    {"Quel est le nom du train ?",
     {"A) Snowpiercer","B) Frostrunner","C) Icebreaker"}, 0},
    {"Pourquoi la Terre est-elle gelee ?",
     {"A) Une eruption volcanique","B) Une substance chimique CW7","C) Un asteroid"}, 1},
    {"Que cherche Namgoong Minsu ?",
     {"A) De la nourriture","B) Une sortie du train","C) Le pouvoir"}, 1},
    {"Quel animal est decouvert vivant a l'exterieur ?",
     {"A) Un loup","B) Un ours polaire","C) Un renard"}, 1},
    {"Quelle est la duree du tour complet du train ?",
     {"A) 6 mois","B) 1 an","C) 2 ans"}, 1},
    {"Comment Curtis arrive-t-il a l'avant du train ?",
     {"A) Par les toits","B) En combattant wagon par wagon","C) Par un tunnel secret"}, 1}
};

int enigme_run(SDL_Window *window, SDL_Renderer *renderer);

static int in_rect(int x, int y, const SDL_Rect *r){
    return (x>=r->x && x<r->x+r->w && y>=r->y && y<r->y+r->h);
}

static SDL_Texture* load_texture(SDL_Renderer *r, const char *path){
    SDL_Surface *s = IMG_Load(path);
    if(!s){ fprintf(stderr,"IMG_Load(%s): %s\n",path,IMG_GetError()); return NULL; }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r,s);
    SDL_FreeSurface(s);
    return t;
}

static SDL_Texture* text_tex(SDL_Renderer *r, TTF_Font *f, const char *txt, SDL_Color c, int *w, int *h){
    SDL_Surface *s = TTF_RenderUTF8_Blended(f,txt,c);
    if(!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r,s);
    if(w) *w=s->w; if(h) *h=s->h;
    SDL_FreeSurface(s);
    return t;
}

static void draw_center(SDL_Renderer *r, TTF_Font *f, const char *txt, int y, int win_w){
    SDL_Color c={255,255,255,255};
    int tw=0,th=0;
    SDL_Texture *t=text_tex(r,f,txt,c,&tw,&th);
    if(t){ SDL_Rect dst={(win_w-tw)/2,y,tw,th}; SDL_RenderCopy(r,t,NULL,&dst); SDL_DestroyTexture(t); }
}

static void draw_panel(SDL_Renderer *r, SDL_Rect rect){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,10,20,30,140); SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,180,240,255,220); SDL_RenderDrawRect(r,&rect);
}

static void draw_button(SDL_Renderer *r, TTF_Font *f, const Button *b){
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    if(b->hovered) SDL_SetRenderDrawColor(r,40,140,200,190);
    else           SDL_SetRenderDrawColor(r,20,50,80,160);
    SDL_RenderFillRect(r,&b->rect);
    if(b->hovered) SDL_SetRenderDrawColor(r,220,250,255,255);
    else           SDL_SetRenderDrawColor(r,140,210,240,220);
    SDL_RenderDrawRect(r,&b->rect);
    SDL_Rect inner={b->rect.x+2,b->rect.y+2,b->rect.w-4,b->rect.h-4};
    SDL_SetRenderDrawColor(r,90,160,190,180); SDL_RenderDrawRect(r,&inner);
    SDL_Color tc={240,250,255,255}; int tw=0,th=0;
    SDL_Texture *t=text_tex(r,f,b->label,tc,&tw,&th);
    if(t){ SDL_Rect dst={b->rect.x+(b->rect.w-tw)/2,b->rect.y+(b->rect.h-th)/2,tw,th};
           SDL_RenderCopy(r,t,NULL,&dst); SDL_DestroyTexture(t); }
}

static void imgbtn_render(SDL_Renderer *r, const ImgButton *b){
    SDL_Texture *t=b->is_hover?b->hover:b->normal;
    if(t) SDL_RenderCopy(r,t,NULL,&b->rect);
}

static void imgbtn_destroy(ImgButton *b){
    if(b->normal){SDL_DestroyTexture(b->normal);b->normal=NULL;}
    if(b->hover) {SDL_DestroyTexture(b->hover); b->hover=NULL;}
}

static void draw_answer_animated(SDL_Renderer *r, TTF_Font *f, const Button *b,
                                 int isSelected, int isCorrect,
                                 int animActive, Uint32 animStart, Uint32 animDuration)
{
    SDL_Rect rect=b->rect;
    float t=0.0f;
    if(animActive){
        Uint32 now=SDL_GetTicks(), dt=(now>animStart)?(now-animStart):0;
        if(dt>animDuration) dt=animDuration;
        t=(float)dt/(float)animDuration;
    }
    Uint8 bgR=b->hovered?40:20, bgG=b->hovered?140:50;
    Uint8 bgB=b->hovered?200:80, bgA=b->hovered?190:160;
    int shakeX=0,pulse=0;
    if(isSelected && animActive){
        if(isCorrect){
            float p=(t<0.5f)?(t/0.5f):((1.0f-t)/0.5f);
            pulse=(int)(6.0f*p); bgR=30;bgG=170;bgB=90;bgA=210;
        } else {
            float s=t*6.0f*3.14159f*2.0f;
            shakeX=(int)(8.0f*sinf(s)); bgR=170;bgG=60;bgB=60;bgA=210;
        }
    }
    rect.x+=shakeX-pulse; rect.y-=pulse; rect.w+=pulse*2; rect.h+=pulse*2;
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,bgR,bgG,bgB,bgA); SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,180,240,255,220); SDL_RenderDrawRect(r,&rect);
    SDL_Rect inner={rect.x+2,rect.y+2,rect.w-4,rect.h-4};
    SDL_SetRenderDrawColor(r,90,160,190,180); SDL_RenderDrawRect(r,&inner);
    SDL_Color tc={240,250,255,255}; int tw=0,th=0;
    SDL_Texture *ttex=text_tex(r,f,b->label,tc,&tw,&th);
    if(ttex){ SDL_Rect dst={rect.x+20,rect.y+(rect.h-th)/2,tw,th};
              SDL_RenderCopy(r,ttex,NULL,&dst); SDL_DestroyTexture(ttex); }
}

static void shuffle_indices(int *arr, int n){
    for(int i=n-1;i>0;--i){int j=rand()%(i+1),tmp=arr[i];arr[i]=arr[j];arr[j]=tmp;}
}

static void set_question_labels(Button *a, Button *b, Button *c, int qIndex){
    strcpy(a->label,quizList[qIndex].answers[0]);
    strcpy(b->label,quizList[qIndex].answers[1]);
    strcpy(c->label,quizList[qIndex].answers[2]);
}

static void pending_rotate_skip(int *pending, int n){
    if(n<=1) return;
    int first=pending[0];
    for(int i=0;i<n-1;i++) pending[i]=pending[i+1];
    pending[n-1]=first;
}

static void pending_pop_answered(int *pending, int *n){
    if(*n<=0) return;
    for(int i=0;i<(*n)-1;i++) pending[i]=pending[i+1];
    (*n)--;
}

int enigme_run(SDL_Window *window, SDL_Renderer *renderer){
    (void)window;
    int W=0,H=0;
    SDL_GetRendererOutputSize(renderer,&W,&H);
    if(W<=0||H<=0){W=1000;H=600;}

    srand((unsigned)time(NULL));
    if(!(IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG)) fprintf(stderr,"IMG_Init: %s\n",IMG_GetError());
    if(TTF_Init()!=0){fprintf(stderr,"TTF_Init: %s\n",TTF_GetError());return 1;}
    int audio_ok=1;
    if(Mix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,2048)<0){
        fprintf(stderr,"Mix_OpenAudio: %s\n",Mix_GetError()); audio_ok=0;
    }

    SDL_Texture *bg_menu=load_texture(renderer,BG_MENU_PATH);
    SDL_Texture *bg_quiz=load_texture(renderer,BG_QUIZ_PATH);
    TTF_Font *title_font=TTF_OpenFont(FONT_PATH,FONT_TITLE);
    TTF_Font *small_font=TTF_OpenFont(FONT_PATH,FONT_SMALL);
    if(!title_font||!small_font){
        fprintf(stderr,"OpenFont error.\n");
        if(bg_menu) SDL_DestroyTexture(bg_menu);
        if(bg_quiz) SDL_DestroyTexture(bg_quiz);
        if(title_font) TTF_CloseFont(title_font);
        if(small_font) TTF_CloseFont(small_font);
        if(audio_ok) Mix_CloseAudio();
        TTF_Quit(); IMG_Quit(); return 1;
    }

    Mix_Music *quizMusic=NULL;
    if(audio_ok){
        quizMusic=Mix_LoadMUS(MUSIC_QUIZ);
        if(!quizMusic) fprintf(stderr,"LoadMUS: %s\n",Mix_GetError());
    }

    Button btnQuiz   = {.rect={W/2-200, H/2-70, 400, 70}, .hovered=0};
    Button btnPuzzle = {.rect={W/2-200, H/2+20, 400, 70}, .hovered=0};
    strcpy(btnQuiz.label,   "QUIZ");
    strcpy(btnPuzzle.label, "PUZZLE");

    ImgButton menuRetour  = (ImgButton){0};
    ImgButton menuQuitter = (ImgButton){0};
    menuRetour.rect  = (SDL_Rect){40,    H-85, 220, 55};
    menuQuitter.rect = (SDL_Rect){W-260, H-85, 220, 55};
    menuRetour.normal  = load_texture(renderer,"retour1.png");
    menuRetour.hover   = load_texture(renderer,"hretour.png");
    menuQuitter.normal = load_texture(renderer,"quitter.png");
    menuQuitter.hover  = load_texture(renderer,"hquitter.png");

    ImgButton quizRetour  = (ImgButton){0};
    ImgButton quizQuitter = (ImgButton){0};
    quizRetour.rect  = (SDL_Rect){40,    H-85, 220, 55};
    quizQuitter.rect = (SDL_Rect){W-260, H-85, 220, 55};
    quizRetour.normal  = load_texture(renderer,"retour1.png");
    quizRetour.hover   = load_texture(renderer,"hretour.png");
    quizQuitter.normal = load_texture(renderer,"quitter.png");
    quizQuitter.hover  = load_texture(renderer,"hquitter.png");

    Button ansA   = {.rect={W/2-320, 210, 640, 58}, .hovered=0};
    Button ansB   = {.rect={W/2-320, 285, 640, 58}, .hovered=0};
    Button ansC   = {.rect={W/2-320, 360, 640, 58}, .hovered=0};
    Button btnSkip= {.rect={W/2-110, 440, 220, 50}, .hovered=0};
    strcpy(btnSkip.label,"SKIP");

    int score=0;
    int pending[NB_QUESTIONS]; int pending_n=0; int currentQ=0;
    int answered=0; char result_msg[128]=""; int selected_choice=-1, is_correct=0;
    int anim_active=0; Uint32 anim_start=0,anim_duration=500;
    int feedback_active=0; Uint32 feedback_start=0;
    const Uint32 FEEDBACK_MS=700;
    int finish_active=0; Uint32 finish_start=0;
    const Uint32 FINISH_MS=1500;
    char finish_msg[128]="";

    State st=ST_MENU;
    int running=1,quit_game=0,music_started=0,ignore_click=0;

    SDL_Event ev;
    while(running){
        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT){quit_game=1;running=0;}
            if(ev.type==SDL_MOUSEBUTTONUP && ev.button.button==SDL_BUTTON_LEFT)
                ignore_click=0;

            if(ev.type==SDL_KEYDOWN){
                if(ev.key.keysym.sym==SDLK_ESCAPE){quit_game=1;running=0;}
                if(st==ST_QUIZ && ev.key.keysym.sym==SDLK_BACKSPACE){
                    st=ST_MENU; music_started=0;
                    answered=0;result_msg[0]='\0';selected_choice=-1;
                    anim_active=0;feedback_active=0;
                    if(audio_ok&&Mix_PlayingMusic()) Mix_HaltMusic();
                }
            }

            if(ev.type==SDL_MOUSEMOTION){
                int mx=ev.motion.x,my=ev.motion.y;
                if(st==ST_MENU){
                    btnQuiz.hovered       = in_rect(mx,my,&btnQuiz.rect);
                    btnPuzzle.hovered     = in_rect(mx,my,&btnPuzzle.rect);
                    menuRetour.is_hover   = in_rect(mx,my,&menuRetour.rect);
                    menuQuitter.is_hover  = in_rect(mx,my,&menuQuitter.rect);
                }
                if(st==ST_QUIZ){
                    quizRetour.is_hover   = in_rect(mx,my,&quizRetour.rect);
                    quizQuitter.is_hover  = in_rect(mx,my,&quizQuitter.rect);
                    ansA.hovered  = in_rect(mx,my,&ansA.rect);
                    ansB.hovered  = in_rect(mx,my,&ansB.rect);
                    ansC.hovered  = in_rect(mx,my,&ansC.rect);
                    btnSkip.hovered = (!answered)?in_rect(mx,my,&btnSkip.rect):0;
                }
            }

            if(ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_LEFT){
                int mx=ev.button.x,my=ev.button.y;

                if(st==ST_MENU){
                    if(in_rect(mx,my,&menuRetour.rect)){
                        quit_game=0; running=0;
                    }
                    else if(in_rect(mx,my,&menuQuitter.rect)){
                        quit_game=1; running=0;
                    }
                    else if(in_rect(mx,my,&btnQuiz.rect)){
                        st=ST_QUIZ; ignore_click=1;
                        for(int i=0;i<NB_QUESTIONS;i++) pending[i]=i;
                        shuffle_indices(pending,NB_QUESTIONS);
                        pending_n=NB_QUESTIONS; currentQ=pending[0];
                        set_question_labels(&ansA,&ansB,&ansC,currentQ);
                        score=0;answered=0;result_msg[0]='\0';
                        selected_choice=-1;anim_active=0;feedback_active=0;
                        if(audio_ok&&quizMusic&&!music_started){
                            Mix_HaltMusic(); Mix_PlayMusic(quizMusic,-1); music_started=1;
                        }
                    }

                }

                if(st==ST_QUIZ){
                    if(ignore_click) continue;
                    if(in_rect(mx,my,&quizRetour.rect)){
                        st=ST_MENU; music_started=0;
                        answered=0;result_msg[0]='\0';selected_choice=-1;
                        anim_active=0;feedback_active=0;
                        if(audio_ok&&Mix_PlayingMusic()) Mix_HaltMusic();
                    }
                    else if(in_rect(mx,my,&quizQuitter.rect)){
                        quit_game=1; running=0;
                    }
                    else if(!answered&&in_rect(mx,my,&btnSkip.rect)){
                        pending_rotate_skip(pending,pending_n);
                        currentQ=pending[0];
                        set_question_labels(&ansA,&ansB,&ansC,currentQ);
                        answered=0;result_msg[0]='\0';selected_choice=-1;
                        anim_active=0;feedback_active=0;
                    }
                    else if(!answered){
                        int choice=-1;
                        if(in_rect(mx,my,&ansA.rect)) choice=0;
                        else if(in_rect(mx,my,&ansB.rect)) choice=1;
                        else if(in_rect(mx,my,&ansC.rect)) choice=2;
                        if(choice!=-1){
                            answered=1; selected_choice=choice;
                            is_correct=(choice==quizList[currentQ].correct);
                            anim_active=1; anim_start=SDL_GetTicks();
                            if(is_correct){
                                score++;
                                snprintf(result_msg,sizeof(result_msg),"Correct !");
                            } else {
                                snprintf(result_msg,sizeof(result_msg),"Faux ! Bonne reponse: %c",
                                         (char)('A'+quizList[currentQ].correct));
                            }
                            feedback_active=1; feedback_start=SDL_GetTicks();
                        }
                    }
                }
            }
        }

        if(anim_active&&SDL_GetTicks()-anim_start>=anim_duration) anim_active=0;

        if(st==ST_QUIZ&&feedback_active&&SDL_GetTicks()-feedback_start>=FEEDBACK_MS){
            feedback_active=0;
            pending_pop_answered(pending,&pending_n);
            if(pending_n<=0){
                st=ST_MENU; music_started=0;
                if(audio_ok&&Mix_PlayingMusic()) Mix_HaltMusic();
                finish_active=1; finish_start=SDL_GetTicks();
                snprintf(finish_msg,sizeof(finish_msg),"Quiz termine ! Score: %d/%d",score,NB_QUESTIONS);
            } else {
                currentQ=pending[0];
                set_question_labels(&ansA,&ansB,&ansC,currentQ);
                answered=0;result_msg[0]='\0';selected_choice=-1;anim_active=0;
            }
        }

        if(finish_active&&SDL_GetTicks()-finish_start>=FINISH_MS) finish_active=0;

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderClear(renderer);

        if(st==ST_MENU){
            if(bg_menu){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_menu,NULL,&d);}
            draw_button(renderer,small_font,&btnQuiz);
            draw_button(renderer,small_font,&btnPuzzle);
            imgbtn_render(renderer,&menuRetour);
            imgbtn_render(renderer,&menuQuitter);
            if(finish_active){
                SDL_Rect panel={W/2-260,80,520,70};
                draw_panel(renderer,panel);
                draw_center(renderer,small_font,finish_msg,98,W);
            }
        }

        else if(st==ST_QUIZ){
            if(bg_quiz){SDL_Rect d={0,0,W,H};SDL_RenderCopy(renderer,bg_quiz,NULL,&d);}
            draw_center(renderer,title_font,"QUIZ",15,W);
            char scoreTxt[64];
            snprintf(scoreTxt,sizeof(scoreTxt),"Score: %d",score);
            draw_center(renderer,small_font,scoreTxt,62,W);

            SDL_Rect qpanel={W/2-360, 95, 720, 95};
            draw_panel(renderer,qpanel);
            SDL_Color qc={240,250,255,255}; int tw=0,th=0;
            SDL_Texture *qt=text_tex(renderer,small_font,quizList[currentQ].question,qc,&tw,&th);
            if(qt){
                SDL_Rect dst={qpanel.x+(qpanel.w-tw)/2, qpanel.y+(qpanel.h-th)/2, tw, th};
                SDL_RenderCopy(renderer,qt,NULL,&dst); SDL_DestroyTexture(qt);
            }

            draw_answer_animated(renderer,small_font,&ansA,selected_choice==0,is_correct,anim_active,anim_start,anim_duration);
            draw_answer_animated(renderer,small_font,&ansB,selected_choice==1,is_correct,anim_active,anim_start,anim_duration);
            draw_answer_animated(renderer,small_font,&ansC,selected_choice==2,is_correct,anim_active,anim_start,anim_duration);

            if(answered&&result_msg[0]) draw_center(renderer,small_font,result_msg,425,W);
            if(!answered) draw_button(renderer,small_font,&btnSkip);

            imgbtn_render(renderer,&quizRetour);
            imgbtn_render(renderer,&quizQuitter);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if(audio_ok&&Mix_PlayingMusic()) Mix_HaltMusic();
    if(quizMusic) Mix_FreeMusic(quizMusic);
    imgbtn_destroy(&menuRetour);  imgbtn_destroy(&menuQuitter);
    imgbtn_destroy(&quizRetour);  imgbtn_destroy(&quizQuitter);
    if(bg_menu) SDL_DestroyTexture(bg_menu);
    if(bg_quiz) SDL_DestroyTexture(bg_quiz);
    TTF_CloseFont(title_font); TTF_CloseFont(small_font);
    if(audio_ok) Mix_CloseAudio();
    TTF_Quit(); IMG_Quit();
    return quit_game?1:0;
}
