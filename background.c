#include "background.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════
   INIT / CHARGEMENT
   ═══════════════════════════════════ */

int background_chargerImages(Background *bg, SDL_Renderer *renderer,
                              const char *cheminImg1, const char *cheminImg2)
{
    SDL_Surface *surf = IMG_Load(cheminImg1);
    if (!surf) {
        fprintf(stderr, "[BG] Erreur '%s' : %s\n", cheminImg1, IMG_GetError());
        return -1;
    }
    bg->imagebackground = SDL_CreateTextureFromSurface(renderer, surf);
    bg->imgW = surf->w;
    bg->imgH = surf->h;
    SDL_FreeSurface(surf);
    bg->imagebackground2 = NULL;
    if (cheminImg2) {
        surf = IMG_Load(cheminImg2);
        if (surf) { bg->imagebackground2 = SDL_CreateTextureFromSurface(renderer, surf); SDL_FreeSurface(surf); }
    }
    return 0;
}

int background_initialiserTexte(Background *bg, const char *contenu,
                                 TTF_Font *police, SDL_Color couleur, int x, int y)
{
    bg->texte.police  = police;
    bg->texte.couleur = couleur;
    strncpy(bg->texte.contenu, contenu, sizeof(bg->texte.contenu) - 1);
    bg->texte.x = x; bg->texte.y = y;
    return 0;
}

int background_initialiserSon(Background *bg, const char *cheminSon)
{
    bg->son = Mix_LoadMUS(cheminSon);
    if (!bg->son) { fprintf(stderr, "[BG] Erreur son : %s\n", Mix_GetError()); return -1; }
    return 0;
}

void background_initialiserPosition(Background *bg, int posx, int posy)
{ bg->posx = posx; bg->posy = posy; }

int background_init(Background *bg, SDL_Renderer *renderer, int niveau)
{
    (void)renderer;
    bg->mode = MODE_MONO;
    bg->tempsDepart = SDL_GetTicks();
    bg->son = NULL;
    bg->imagebackground = bg->imagebackground2 = NULL;
    bg->fenetreguide.visible = 0; bg->fenetreguide.texture = NULL;
    bg->texte.police = NULL; bg->texte.contenu[0] = '\0';
    bg->posx = bg->posy = 0;
    bg->w = W_BACKGROUND; bg->h = H_BACKGROUND;
    bg->imgW = bg->imgH = 0;
    bg->vue = VUE_INTERIEURE;
    bg->wagonActif = 0;
    bg->nbWagons = bg->nbPortes = bg->nbPlateformes = bg->nbDestructibles = 0;
    bg->flash.actif = bg->flash.alpha = 0;
    bg->parallaxOffset = 0.0f;
    bg->transitionSlide.actif = 0;
    bg->transitionSlide.offset = 0.0f;
    bg->transitionSlide.direction = 1;
    bg->transitionSlide.wagonCible = 0;
    bg->camera1 = (SDL_Rect){0, 0, W_ECRAN, H_ECRAN};
    bg->camera2 = (SDL_Rect){0, 0, W_ECRAN/2, H_ECRAN};
    bg->posEcran1 = (SDL_Rect){0, 0, W_ECRAN/2, H_ECRAN};
    bg->posEcran2 = (SDL_Rect){W_ECRAN/2, 0, W_ECRAN/2, H_ECRAN};
    switch (niveau) {
        case 1:  bg->typePlateforme = PLATEFORME_FIXE;         break;
        case 2:  bg->typePlateforme = PLATEFORME_MOBILE;       break;
        default: bg->typePlateforme = PLATEFORME_DESTRUCTIBLE; break;
    }
    bg->niveau = niveau;
    /* Phase 2 init */
    for (int i = 0; i < MAX_WAGONS; i++) bg->wagonBgTex[i] = NULL;
    bg->outsideBgTex = NULL;
    bg->playerSheet  = NULL;
    for (int i = 0; i < 3; i++) bg->enemySheets[i] = NULL;
    bg->bulletTex    = NULL;
    bg->muzzleTex    = NULL;
    bg->hitFxSheet   = NULL;
    bg->nbBalles     = 0;
    bg->nbEnnemis    = 0;
    bg->nbHitFx      = 0;
    memset(bg->balles,  0, sizeof(bg->balles));
    memset(bg->ennemis, 0, sizeof(bg->ennemis));
    memset(bg->hitFx,   0, sizeof(bg->hitFx));
    return 0;
}

/* ═══════════════════════════════════
   LEGACY scrolling/fond (utilisé pour scores)
   ═══════════════════════════════════ */

void background_scrolling(Background *bg, int direction, int dx, int dy, int numBack)
{
    SDL_Rect *cam = (numBack == 2) ? &bg->camera2 : &bg->camera1;
    switch (direction) {
        case SCROLL_DROITE: if (cam->x + W_ECRAN + dx <= bg->w) cam->x += dx; break;
        case SCROLL_GAUCHE: if (cam->x - dx >= 0) cam->x -= dx;               break;
        case SCROLL_HAUT:   if (cam->y - dy >= 0) cam->y -= dy;               break;
        case SCROLL_BAS:    if (cam->y + cam->h + dy <= bg->h) cam->y += dy;  break;
        default: break;
    }
}

void background_afficher(Background *bg, SDL_Renderer *renderer, int mode, int deltax, int deltay)
{
    (void)deltax; (void)deltay; (void)mode;
    if (!bg->imagebackground) return;
    float scaleX = (float)bg->imgW / (float)bg->w;
    float scaleY = (float)bg->imgH / (float)bg->h;
    SDL_Rect src = {(int)(bg->camera1.x*scaleX),(int)(bg->camera1.y*scaleY),
                    (int)(W_ECRAN*scaleX),(int)(H_ECRAN*scaleY)};
    if (src.x < 0) src.x = 0;
    if (src.y < 0) src.y = 0;
    if (src.x + src.w > bg->imgW) src.x = bg->imgW - src.w;
    if (src.y + src.h > bg->imgH) src.y = bg->imgH - src.h;
    SDL_RenderCopy(renderer, bg->imagebackground, &src, NULL);
}

void background_afficherTemps(Background *bg, SDL_Renderer *renderer, int x, int y)
{
    if (!bg->texte.police) return;
    Uint32 e = SDL_GetTicks() - bg->tempsDepart;
    char buf[32]; snprintf(buf, sizeof(buf), "Temps : %02u:%02u", (e/1000)/60, (e/1000)%60);
    SDL_Surface *s = TTF_RenderUTF8_Blended(bg->texte.police, buf, bg->texte.couleur);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_Rect d = {x, y, s->w, s->h}; SDL_RenderCopy(renderer, t, NULL, &d);
    SDL_DestroyTexture(t); SDL_FreeSurface(s);
}

/* ═══ GUIDE ═══ */

int fenetreGuide_init(Background *bg, SDL_Renderer *renderer, const char *cheminImage, SDL_Rect rect)
{
    SDL_Surface *surf = IMG_Load(cheminImage);
    if (!surf) { fprintf(stderr, "[Guide] Erreur '%s' : %s\n", cheminImage, IMG_GetError()); return -1; }
    bg->fenetreguide.texture  = SDL_CreateTextureFromSurface(renderer, surf);
    bg->fenetreguide.position = rect;
    bg->fenetreguide.visible  = 0;
    SDL_FreeSurface(surf);
    return 0;
}

void fenetreGuide_afficher(Background *bg, SDL_Renderer *renderer)
{
    if (!bg->fenetreguide.visible || !bg->fenetreguide.texture) return;
    SDL_RenderCopy(renderer, bg->fenetreguide.texture, NULL, &bg->fenetreguide.position);
}

/* ═══ SCORES ═══ */

void scores_saisirNom(char *nom, int taille, SDL_Renderer *renderer, TTF_Font *police, SDL_Color couleur)
{
    SDL_StartTextInput();
    int idx=0, done=0; nom[0]='\0';
    SDL_Event ev; Uint32 lb=SDL_GetTicks(); int sc=1;
    while (!done) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type==SDL_QUIT) { done=1; }
            else if (ev.type==SDL_KEYDOWN) {
                if (ev.key.keysym.sym==SDLK_RETURN||ev.key.keysym.sym==SDLK_KP_ENTER) { if(idx>0) done=1; }
                else if (ev.key.keysym.sym==SDLK_BACKSPACE && idx>0) nom[--idx]='\0';
            } else if (ev.type==SDL_TEXTINPUT && idx<taille-1) { nom[idx++]=ev.text.text[0]; nom[idx]='\0'; }
        }
        SDL_SetRenderDrawColor(renderer,20,20,40,255); SDL_RenderClear(renderer);
        if (police) {
            SDL_Color blanc={255,255,255,255};
            SDL_Surface *s=TTF_RenderUTF8_Blended(police,"Entrez votre nom :",blanc);
            if(s){SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d={W_ECRAN/2-s->w/2,H_ECRAN/2-60,s->w,s->h};SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);}
            Uint32 now=SDL_GetTicks(); if(now-lb>500){sc=!sc;lb=now;}
            char aff[130]; snprintf(aff,sizeof(aff),"%s%s",nom,sc?"|":" ");
            s=TTF_RenderUTF8_Blended(police,aff,couleur);
            if(s){SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d={W_ECRAN/2-s->w/2,H_ECRAN/2,s->w,s->h};SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);}
        }
        SDL_RenderPresent(renderer); SDL_Delay(16);
    }
    SDL_StopTextInput();
}

void scores_sauvegarder(const char *f, Score *scores, int nb)
{ FILE*fp=fopen(f,"w"); if(!fp) return; fprintf(fp,"%d\n",nb); for(int i=0;i<nb;i++) fprintf(fp,"%s %d\n",scores[i].nom,scores[i].score); fclose(fp); }

int scores_charger(const char *f, Score *scores, int max)
{
    FILE*fp=fopen(f,"r"); if(!fp) return 0;
    int nb=0; fscanf(fp,"%d\n",&nb); if(nb>max) nb=max;
    for(int i=0;i<nb;i++) if(fscanf(fp,"%63s %d\n",scores[i].nom,&scores[i].score)!=2){nb=i;break;}
    fclose(fp); return nb;
}

void scores_ajouterEtTrier(Score *scores, int *nb, const char *nom, int score)
{
    if(*nb<MAX_SCORES){strncpy(scores[*nb].nom,nom,63);scores[(*nb)++].score=score;}
    else if(score>scores[MAX_SCORES-1].score){strncpy(scores[MAX_SCORES-1].nom,nom,63);scores[MAX_SCORES-1].score=score;}
    for(int i=0;i<*nb-1;i++) for(int j=0;j<*nb-i-1;j++) if(scores[j].score<scores[j+1].score){Score t=scores[j];scores[j]=scores[j+1];scores[j+1]=t;}
}

void scores_afficherMenu(Score *scores, int nb, SDL_Renderer *renderer, TTF_Font *police, SDL_Color couleur)
{
    if(!police) return;
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,0,0,0,180);
    SDL_Rect fond={0,0,W_ECRAN,H_ECRAN}; SDL_RenderFillRect(renderer,&fond);
    SDL_Color dore={255,215,0,255};
    SDL_Surface*s=TTF_RenderUTF8_Blended(police,"=== Meilleurs Scores ===",dore);
    if(s){SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d={W_ECRAN/2-s->w/2,50,s->w,s->h};SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);}
    for(int i=0;i<nb;i++){
        char l[128]; snprintf(l,sizeof(l),"%2d.  %-20s  %d",i+1,scores[i].nom,scores[i].score);
        s=TTF_RenderUTF8_Blended(police,l,couleur); if(!s) continue;
        SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);
        SDL_Rect d={W_ECRAN/2-s->w/2,110+i*(s->h+6),s->w,s->h};
        SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);
    }
    SDL_Color gris={200,200,200,255};
    s=TTF_RenderUTF8_Blended(police,"Appuyez sur ECHAP pour revenir",gris);
    if(s){SDL_Texture*t=SDL_CreateTextureFromSurface(renderer,s);SDL_Rect d={W_ECRAN/2-s->w/2,H_ECRAN-40,s->w,s->h};SDL_RenderCopy(renderer,t,NULL,&d);SDL_DestroyTexture(t);SDL_FreeSurface(s);}
}

/* ═══ LEGACY — vues/plateformes/destructibles init (stubs) ═══ */

void vues_init(Background *bg, SDL_Renderer *renderer)
{
    const char *f[MAX_WAGONS]={"wagon1.png","wagon2.png","wagon3.png","wagon4.png","wagon5.png"};
    const char *n[MAX_WAGONS]={"Wagon Pauvres","Wagon Industrie","Wagon Aquarium","Wagon Jardin","Wagon Luxe"};
    bg->nbWagons=MAX_WAGONS;
    for(int i=0;i<MAX_WAGONS;i++){
        SDL_Surface*s=IMG_Load(f[i]);
        bg->wagons[i].texture=s?SDL_CreateTextureFromSurface(renderer,s):NULL;
        if(s) SDL_FreeSurface(s);
        strncpy(bg->wagons[i].nom,n[i],31);
    }
}

void plateformes_init(Background *bg, SDL_Renderer *renderer) { (void)renderer; bg->nbPlateformes=0; }
void plateformes_update(Background *bg) { (void)bg; }
int  plateforme_collision(Background *bg, SDL_Rect *j, int *v) { (void)bg;(void)j;(void)v; return 0; }
void destructibles_init(Background *bg, SDL_Renderer *renderer) { (void)renderer; bg->nbDestructibles=0; }
void destructibles_update(Background *bg) { (void)bg; }
int  destructibles_verifierCollision(Background *bg, SDL_Rect j) { (void)bg;(void)j; return 0; }
void destructibles_liberer(Background *bg) { (void)bg; }

/* ═══ NEIGE ═══ */

void neige_init(Background *bg)
{
    Uint32 seed=SDL_GetTicks();
    for(int i=0;i<MAX_NEIGE;i++){
        ParticuleNeige*p=&bg->neige[i]; p->couche=i%3;
        seed=seed*1664525u+1013904223u; p->x=(float)((seed>>16)%W_ECRAN);
        seed=seed*1664525u+1013904223u; p->y=(float)((seed>>16)%H_ECRAN);
        seed=seed*1664525u+1013904223u; float r=(float)((seed>>16)%100)/100.0f;
        switch(p->couche){
            case 0:p->vitesse=0.3f+r*0.3f;p->alpha=55;p->taille=1;break;
            case 1:p->vitesse=0.8f+r*0.5f;p->alpha=110;p->taille=2;break;
            default:p->vitesse=1.6f+r*0.9f;p->alpha=180;p->taille=3;break;
        }
        seed=seed*1664525u+1013904223u;
        p->derive=((float)((seed>>16)%60)-30.0f)/100.0f;
    }
}

void neige_update(Background *bg)
{
    Uint32 t=SDL_GetTicks();
    for(int i=0;i<MAX_NEIGE;i++){
        ParticuleNeige*p=&bg->neige[i];
        float wave=(float)SDL_sin((double)(t*0.001+p->y*0.02))*0.3f;
        p->y+=p->vitesse; p->x+=p->derive+wave;
        if(p->y>H_ECRAN+4){ p->y=-4.0f; Uint32 s=(Uint32)(p->x*7919u)^t; s=s*1664525u+1013904223u; p->x=(float)((s>>16)%W_ECRAN); }
        if(p->x<-4) p->x=(float)W_ECRAN;
        if(p->x>W_ECRAN+4) p->x=0.0f;
    }
}

/* ═══ FLASH ═══ */

void flash_declencher(Background *bg) { bg->flash.actif=1; bg->flash.alpha=170; }

void flash_update(Background *bg)
{ if(!bg->flash.actif) return; bg->flash.alpha-=18; if(bg->flash.alpha<=0){bg->flash.alpha=0;bg->flash.actif=0;} }

void flash_afficher(Background *bg, SDL_Renderer *renderer)
{
    if(!bg->flash.actif||bg->flash.alpha<=0) return;
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,190,225,255,(Uint8)bg->flash.alpha);
    SDL_Rect f={0,0,W_ECRAN,H_ECRAN}; SDL_RenderFillRect(renderer,&f);
}

/* ═══════════════════════════════════════════════════
   WAGONS INTÉRIEURS — INIT DES STYLES
   ═══════════════════════════════════════════════════ */

void wagonsInterieurs_init(Background *bg, SDL_Renderer *renderer)
{
    /* Wagon 0 – Pauvres */
    { StyleWagon*s=&bg->stylesWagons[0];
      s->fond=(SDL_Color){36,28,22,255}; s->sol=(SDL_Color){52,40,30,255};
      s->plafond=(SDL_Color){42,33,26,255}; s->mur=(SDL_Color){48,37,29,255};
      s->accent=(SDL_Color){160,90,35,255}; s->accentClair=(SDL_Color){210,130,55,255};
      s->fenetres[0]=(SDL_Rect){110,WAGON_PLAF_Y+18,130,88};
      s->fenetres[1]=(SDL_Rect){560,WAGON_PLAF_Y+18,130,88}; s->nbFenetres=2;
      s->obstacles.nb=3;
      s->obstacles.rects[0]=(SDL_Rect){75,WAGON_SOL_Y-42,85,42};  s->obstacles.couleurs[0]=(SDL_Color){62,48,36,255};
      s->obstacles.rects[1]=(SDL_Rect){320,WAGON_SOL_Y-65,100,22};s->obstacles.couleurs[1]=(SDL_Color){58,44,33,255};
      s->obstacles.rects[2]=(SDL_Rect){600,WAGON_SOL_Y-42,85,42}; s->obstacles.couleurs[2]=(SDL_Color){62,48,36,255};
    }
    /* Wagon 1 – Industrie */
    { StyleWagon*s=&bg->stylesWagons[1];
      s->fond=(SDL_Color){30,34,42,255}; s->sol=(SDL_Color){48,54,66,255};
      s->plafond=(SDL_Color){36,40,50,255}; s->mur=(SDL_Color){42,48,60,255};
      s->accent=(SDL_Color){60,200,80,255}; s->accentClair=(SDL_Color){100,240,120,255};
      s->fenetres[0]=(SDL_Rect){290,WAGON_PLAF_Y+22,220,78}; s->nbFenetres=1;
      s->obstacles.nb=4;
      s->obstacles.rects[0]=(SDL_Rect){55,WAGON_SOL_Y-90,55,90};  s->obstacles.couleurs[0]=(SDL_Color){52,58,72,255};
      s->obstacles.rects[1]=(SDL_Rect){190,WAGON_SOL_Y-22,130,22};s->obstacles.couleurs[1]=(SDL_Color){56,63,78,255};
      s->obstacles.rects[2]=(SDL_Rect){480,WAGON_SOL_Y-22,130,22};s->obstacles.couleurs[2]=(SDL_Color){56,63,78,255};
      s->obstacles.rects[3]=(SDL_Rect){690,WAGON_SOL_Y-90,55,90}; s->obstacles.couleurs[3]=(SDL_Color){52,58,72,255};
    }
    /* Wagon 2 – Aquarium */
    { StyleWagon*s=&bg->stylesWagons[2];
      s->fond=(SDL_Color){6,18,52,255}; s->sol=(SDL_Color){10,36,64,255};
      s->plafond=(SDL_Color){6,14,44,255}; s->mur=(SDL_Color){8,24,58,255};
      s->accent=(SDL_Color){35,195,210,255}; s->accentClair=(SDL_Color){75,230,245,255};
      s->fenetres[0]=(SDL_Rect){72,WAGON_PLAF_Y+12,185,108};
      s->fenetres[1]=(SDL_Rect){543,WAGON_PLAF_Y+12,185,108}; s->nbFenetres=2;
      s->obstacles.nb=3;
      s->obstacles.rects[0]=(SDL_Rect){140,WAGON_SOL_Y-44,115,18};s->obstacles.couleurs[0]=(SDL_Color){12,50,85,255};
      s->obstacles.rects[1]=(SDL_Rect){330,WAGON_SOL_Y-88,140,18};s->obstacles.couleurs[1]=(SDL_Color){9,42,76,255};
      s->obstacles.rects[2]=(SDL_Rect){545,WAGON_SOL_Y-44,115,18};s->obstacles.couleurs[2]=(SDL_Color){12,50,85,255};
    }
    /* Wagon 3 – Jardin */
    { StyleWagon*s=&bg->stylesWagons[3];
      s->fond=(SDL_Color){16,30,16,255}; s->sol=(SDL_Color){36,25,14,255};
      s->plafond=(SDL_Color){12,25,12,255}; s->mur=(SDL_Color){20,36,20,255};
      s->accent=(SDL_Color){75,200,70,255}; s->accentClair=(SDL_Color){115,240,95,255};
      s->fenetres[0]=(SDL_Rect){55,WAGON_PLAF_Y+12,160,100};
      s->fenetres[1]=(SDL_Rect){585,WAGON_PLAF_Y+12,160,100}; s->nbFenetres=2;
      s->obstacles.nb=4;
      s->obstacles.rects[0]=(SDL_Rect){85,WAGON_SOL_Y-36,72,36};  s->obstacles.couleurs[0]=(SDL_Color){38,58,22,255};
      s->obstacles.rects[1]=(SDL_Rect){248,WAGON_SOL_Y-54,95,18}; s->obstacles.couleurs[1]=(SDL_Color){32,50,18,255};
      s->obstacles.rects[2]=(SDL_Rect){457,WAGON_SOL_Y-54,95,18}; s->obstacles.couleurs[2]=(SDL_Color){32,50,18,255};
      s->obstacles.rects[3]=(SDL_Rect){643,WAGON_SOL_Y-36,72,36}; s->obstacles.couleurs[3]=(SDL_Color){38,58,22,255};
    }
    /* Wagon 4 – Luxe */
    { StyleWagon*s=&bg->stylesWagons[4];
      s->fond=(SDL_Color){36,14,14,255}; s->sol=(SDL_Color){55,28,14,255};
      s->plafond=(SDL_Color){40,10,10,255}; s->mur=(SDL_Color){46,18,16,255};
      s->accent=(SDL_Color){215,175,55,255}; s->accentClair=(SDL_Color){255,215,85,255};
      s->fenetres[0]=(SDL_Rect){145,WAGON_PLAF_Y+12,158,100};
      s->fenetres[1]=(SDL_Rect){497,WAGON_PLAF_Y+12,158,100}; s->nbFenetres=2;
      s->obstacles.nb=3;
      s->obstacles.rects[0]=(SDL_Rect){125,WAGON_SOL_Y-50,105,18};s->obstacles.couleurs[0]=(SDL_Color){62,35,18,255};
      s->obstacles.rects[1]=(SDL_Rect){345,WAGON_SOL_Y-82,110,18};s->obstacles.couleurs[1]=(SDL_Color){58,32,16,255};
      s->obstacles.rects[2]=(SDL_Rect){570,WAGON_SOL_Y-50,105,18};s->obstacles.couleurs[2]=(SDL_Color){62,35,18,255};
    }

    /* Ensure texture pointers start NULL */
    for (int i = 0; i < MAX_WAGONS; i++) {
        bg->stylesWagons[i].floorTex    = NULL;
        bg->stylesWagons[i].obstacleTex = NULL;
    }

    /* Load floor and obstacle textures */
    const char *floorFiles[MAX_WAGONS] = {
        "wagon0_floor.png","wagon1_floor.png","wagon2_floor.png",
        "wagon3_floor.png","wagon4_floor.png"
    };
    const char *obstacleFiles[MAX_WAGONS] = {
        "obstacle_poor.png","obstacle_industry.png","obstacle_aquarium.png",
        "obstacle_garden.png","obstacle_luxury.png"
    };
    for (int i = 0; i < MAX_WAGONS; i++) {
        SDL_Surface *surf = IMG_Load(floorFiles[i]);
        bg->stylesWagons[i].floorTex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
        if (surf) SDL_FreeSurface(surf);

        surf = IMG_Load(obstacleFiles[i]);
        bg->stylesWagons[i].obstacleTex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
        if (surf) SDL_FreeSurface(surf);
    }

    /* ── Phase 2: wagon backgrounds ── */
    const char *wagonBgFiles[MAX_WAGONS] = {
        "wagon0.png","wagon1.png","wagon2.png","wagon3.png","wagon4.png"
    };
    for (int i = 0; i < MAX_WAGONS; i++) {
        SDL_Surface *surf = IMG_Load(wagonBgFiles[i]);
        bg->wagonBgTex[i] = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
        if (surf) SDL_FreeSurface(surf);
    }

    /* Outside parallax */
    SDL_Surface *surf = IMG_Load("outside_bg.png");
    bg->outsideBgTex = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);

    /* Player & enemy spritesheets */
    bg->playerSheet            = combat_loadTexture(renderer, "player.png");
    bg->enemySheets[ET_GUARD]    = combat_loadTexture(renderer, "enemy_guard.png");
    bg->enemySheets[ET_WORKER]   = combat_loadTexture(renderer, "enemy_worker.png");
    bg->enemySheets[ET_ENFORCER] = combat_loadTexture(renderer, "enemy_enforcer.png");

    /* Effects */
    bg->bulletTex  = combat_loadTexture(renderer, "bullet.png");
    bg->muzzleTex  = combat_loadTexture(renderer, "muzzle.png");
    bg->hitFxSheet = combat_loadTexture(renderer, "hit_effect.png");

    /* Spawn enemies per wagon */
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_WORKER,   580, WAGON_SOL_Y-WORKER_HBH,   0, bg->enemySheets[ET_WORKER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    620, WAGON_SOL_Y-GUARD_HBH,    1, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_WORKER,   530, WAGON_SOL_Y-WORKER_HBH,   1, bg->enemySheets[ET_WORKER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    560, WAGON_SOL_Y-GUARD_HBH,    2, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    640, WAGON_SOL_Y-GUARD_HBH,    2, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_WORKER,   590, WAGON_SOL_Y-WORKER_HBH,   3, bg->enemySheets[ET_WORKER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    550, WAGON_SOL_Y-GUARD_HBH,    3, bg->enemySheets[ET_GUARD]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_ENFORCER, 580, WAGON_SOL_Y-ENFORCER_HBH, 4, bg->enemySheets[ET_ENFORCER]);
    ennemis_spawn(bg->ennemis, &bg->nbEnnemis, ET_GUARD,    650, WAGON_SOL_Y-GUARD_HBH,    4, bg->enemySheets[ET_GUARD]);

    bg->wagonActif = 0;
    bg->parallaxOffset = 0.0f;
    bg->transitionSlide.actif = 0;
}

/* ═══════════════════════════════════════════════════
   DESSIN D'UN WAGON (offset X pour transition)
   ═══════════════════════════════════════════════════ */

static void wagon_dessiner(Background *bg, SDL_Renderer *renderer, int idx, int ox)
{
    if (idx < 0 || idx >= MAX_WAGONS) return;
    StyleWagon *s = &bg->stylesWagons[idx];

    SDL_Rect clip = {ox, 0, W_ECRAN, H_ECRAN};
    SDL_RenderSetClipRect(renderer, &clip);

    /* Fond — wagon background image or fallback solid color */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    if (bg->wagonBgTex[idx]) {
        SDL_RenderCopy(renderer, bg->wagonBgTex[idx], NULL, &clip);
    } else {
        SDL_SetRenderDrawColor(renderer, s->fond.r, s->fond.g, s->fond.b, 255);
        SDL_RenderFillRect(renderer, &clip);
    }

    /* Fenêtres */
    for (int i = 0; i < s->nbFenetres; i++) {
        SDL_Rect fw = {s->fenetres[i].x + ox, s->fenetres[i].y,
                       s->fenetres[i].w,       s->fenetres[i].h};

        /* Ciel nocturne */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 10, 15, 30, 255);
        SDL_RenderFillRect(renderer, &fw);

        /* Parallax extérieure — use outsideBgTex, fallback to imagebackground */
        SDL_Texture *parTex = bg->outsideBgTex ? bg->outsideBgTex : bg->imagebackground;
        if (parTex) {
            int parW, parH;
            SDL_QueryTexture(parTex, NULL, NULL, &parW, &parH);
            SDL_RenderSetClipRect(renderer, &fw);
            int srcX = (int)bg->parallaxOffset % parW;
            if (srcX < 0) srcX += parW;
            int srcW = fw.w;
            int srcH = parH;
            if (srcX + srcW <= parW) {
                SDL_Rect src = {srcX, 0, srcW, srcH};
                SDL_RenderCopy(renderer, parTex, &src, &fw);
            } else {
                int w1 = parW - srcX;
                SDL_Rect s1={srcX,0,w1,srcH}, d1={fw.x,fw.y,w1,fw.h};
                SDL_RenderCopy(renderer, parTex, &s1, &d1);
                int w2 = srcW - w1;
                SDL_Rect s2={0,0,w2,srcH}, d2={fw.x+w1,fw.y,w2,fw.h};
                SDL_RenderCopy(renderer, parTex, &s2, &d2);
            }
            SDL_RenderSetClipRect(renderer, &clip);
        }

        /* Givre sur la vitre */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 100, 150, 220, 20);
        SDL_RenderFillRect(renderer, &fw);

        /* Cadre épais */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer,
            SDL_min(s->mur.r+28,255), SDL_min(s->mur.g+28,255), SDL_min(s->mur.b+28,255), 255);
        for (int t = 0; t < 3; t++) {
            SDL_Rect b = {fw.x-t, fw.y-t, fw.w+2*t, fw.h+2*t};
            SDL_RenderDrawRect(renderer, &b);
        }
    }

    /* Plafond */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, s->plafond.r, s->plafond.g, s->plafond.b, 255);
    SDL_Rect plaf = {ox, 0, W_ECRAN, WAGON_PLAF_Y};
    SDL_RenderFillRect(renderer, &plaf);
    SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
    SDL_RenderDrawLine(renderer, ox, WAGON_PLAF_Y,   ox+W_ECRAN, WAGON_PLAF_Y);
    SDL_RenderDrawLine(renderer, ox, WAGON_PLAF_Y+1, ox+W_ECRAN, WAGON_PLAF_Y+1);

    /* Sol */
    SDL_SetRenderDrawColor(renderer, s->sol.r, s->sol.g, s->sol.b, 255);
    SDL_Rect sol = {ox, WAGON_SOL_Y, W_ECRAN, H_ECRAN - WAGON_SOL_Y};
    SDL_RenderFillRect(renderer, &sol);
    if (s->floorTex) {
        SDL_RenderCopy(renderer, s->floorTex, NULL, &sol);
    }
    SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
    SDL_RenderDrawLine(renderer, ox, WAGON_SOL_Y-1, ox+W_ECRAN, WAGON_SOL_Y-1);
    SDL_RenderDrawLine(renderer, ox, WAGON_SOL_Y,   ox+W_ECRAN, WAGON_SOL_Y);

    /* Murs gauche/droite */
    SDL_SetRenderDrawColor(renderer, s->mur.r, s->mur.g, s->mur.b, 255);
    SDL_Rect murG = {ox,               WAGON_PLAF_Y, WAGON_MUR_G_X,              WAGON_SOL_Y-WAGON_PLAF_Y};
    SDL_Rect murD = {ox+WAGON_MUR_D_X, WAGON_PLAF_Y, W_ECRAN-WAGON_MUR_D_X,     WAGON_SOL_Y-WAGON_PLAF_Y};
    SDL_RenderFillRect(renderer, &murG);
    SDL_RenderFillRect(renderer, &murD);

    /* Portes */
    int py   = WAGON_SOL_Y - WAGON_PORTE_H;
    int px_l = ox + WAGON_MUR_G_X;
    int px_r = ox + WAGON_MUR_D_X - WAGON_PORTE_W;

    if (idx > 0) {
        SDL_SetRenderDrawColor(renderer, s->fond.r+8, s->fond.g+8, s->fond.b+8, 255);
        SDL_Rect pg = {px_l, py, WAGON_PORTE_W, WAGON_PORTE_H};
        SDL_RenderFillRect(renderer, &pg);
        SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
        SDL_RenderDrawRect(renderer, &pg);
        SDL_Rect kR = {px_l+WAGON_PORTE_W-8, py+WAGON_PORTE_H/2-8, 5, 16};
        SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
        SDL_RenderFillRect(renderer, &kR);
    }

    if (idx < MAX_WAGONS-1) {
        SDL_SetRenderDrawColor(renderer, s->fond.r+8, s->fond.g+8, s->fond.b+8, 255);
        SDL_Rect pd = {px_r, py, WAGON_PORTE_W, WAGON_PORTE_H};
        SDL_RenderFillRect(renderer, &pd);
        SDL_SetRenderDrawColor(renderer, s->accent.r, s->accent.g, s->accent.b, 255);
        SDL_RenderDrawRect(renderer, &pd);
        SDL_Rect kL = {px_r+3, py+WAGON_PORTE_H/2-8, 5, 16};
        SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
        SDL_RenderFillRect(renderer, &kL);
    }

    /* Obstacles */
    for (int i = 0; i < s->obstacles.nb; i++) {
        SDL_Rect r = {s->obstacles.rects[i].x+ox, s->obstacles.rects[i].y,
                      s->obstacles.rects[i].w,     s->obstacles.rects[i].h};
        if (s->obstacleTex) {
            SDL_RenderCopy(renderer, s->obstacleTex, NULL, &r);
        } else {
            SDL_Color c = s->obstacles.couleurs[i];
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
            SDL_RenderFillRect(renderer, &r);
            SDL_SetRenderDrawColor(renderer, SDL_min(c.r+28,255), SDL_min(c.g+28,255), SDL_min(c.b+28,255), 255);
            SDL_RenderDrawLine(renderer, r.x, r.y, r.x+r.w, r.y);
            SDL_SetRenderDrawColor(renderer, s->accent.r/3, s->accent.g/3, s->accent.b/3, 255);
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    /* Lampe centrale */
    int lx = ox + W_ECRAN/2;
    SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 255);
    SDL_Rect lamp = {lx-9, WAGON_PLAF_Y+2, 18, 10};
    SDL_RenderFillRect(renderer, &lamp);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, s->accentClair.r, s->accentClair.g, s->accentClair.b, 22);
    SDL_Rect halo = {lx-90, WAGON_PLAF_Y, 180, 90};
    SDL_RenderFillRect(renderer, &halo);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_RenderSetClipRect(renderer, NULL);
}

/* ═══════════════════════════════════════════════════
   JOUEUR — INTÉRIEUR
   ═══════════════════════════════════════════════════ */

void joueur_init_interieur(Background *bg)
{
    bg->joueur.x    = (float)(W_ECRAN/2 - JOUEUR_W/2);
    bg->joueur.y    = (float)(WAGON_SOL_Y - JOUEUR_H);
    bg->joueur.velX = bg->joueur.velY = 0.0f;
    bg->joueur.w    = JOUEUR_W;
    bg->joueur.h    = JOUEUR_H;
    bg->joueur.auSol      = 1;
    bg->joueur.actif      = 1;
    bg->joueur.direction  = 1;
    bg->joueur.hp         = JOUEUR_MAX_HP;
    bg->joueur.etat       = PS_IDLE;
    bg->joueur.lastShootTick = 0;
    bg->joueur.lastMeleeTick = 0;
    bg->joueur.muzzleFrames  = 0;
    animation_init(&bg->joueur.anim, bg->playerSheet,
                   PLAYER_COLS, PLAYER_ROWS,
                   ANIM_P_IDLE, PLAYER_NFRAMES[ANIM_P_IDLE],
                   PLAYER_ANIM_FPS[ANIM_P_IDLE], PLAYER_ANIM_LOOP[ANIM_P_IDLE]);
}

void joueur_update_interieur(Background *bg, int gauche, int droite,
                              int sauter, int tirer, int melee, float dt)
{
    Joueur *j = &bg->joueur;
    if (!j->actif) return;

    Uint32 now = SDL_GetTicks();

    /* Always tick animation */
    animation_update(&j->anim, dt);

    /* Dead: no input, hold */
    if (j->etat == PS_DEAD) return;

    /* Hit: wait for animation then recover */
    if (j->etat == PS_HIT) {
        if (j->anim.finished) {
            j->etat = PS_IDLE;
            animation_set_row(&j->anim, ANIM_P_IDLE,
                              PLAYER_NFRAMES[ANIM_P_IDLE],
                              PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
        }
        j->velY += GRAVITE_JEU;
        if (j->velY > 18.0f) j->velY = 18.0f;
        j->y += j->velY;
        if (j->y + j->h >= (float)WAGON_SOL_Y) {
            j->y = (float)(WAGON_SOL_Y - j->h); j->velY = 0; j->auSol = 1;
        }
        return;
    }

    /* Shoot / Melee: let animation finish first, then return to idle */
    if (j->etat == PS_SHOOT || j->etat == PS_MELEE) {
        if (j->anim.finished) {
            j->etat = PS_IDLE;
            animation_set_row(&j->anim, ANIM_P_IDLE,
                              PLAYER_NFRAMES[ANIM_P_IDLE],
                              PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
        }
        if (j->muzzleFrames > 0) j->muzzleFrames--;
        /* still apply gravity during action */
        j->velY += GRAVITE_JEU;
        if (j->velY > 18.0f) j->velY = 18.0f;
        j->y += j->velY;
        j->auSol = 0;
        if (j->y + j->h >= (float)WAGON_SOL_Y) {
            j->y = (float)(WAGON_SOL_Y - j->h); j->velY = 0; j->auSol = 1;
        }
        return;
    }

    if (bg->transitionSlide.actif) return;

    int idx = bg->wagonActif;
    StyleWagon *s = &bg->stylesWagons[idx];

    /* ── Combat actions ── */
    if (tirer && (int)(now - j->lastShootTick) >= SHOOT_COOLDOWN_MS) {
        j->lastShootTick = now;
        j->etat = PS_SHOOT;
        animation_set_row(&j->anim, ANIM_P_SHOOT,
                          PLAYER_NFRAMES[ANIM_P_SHOOT],
                          PLAYER_ANIM_FPS[ANIM_P_SHOOT], 0);
        j->muzzleFrames = 3;
        j->muzzleX = j->x + (j->direction == 1 ? (float)JOUEUR_W + 4 : -4.0f);
        j->muzzleY = j->y + JOUEUR_H * 0.35f;

        /* Spawn bullet */
        if (bg->nbBalles < MAX_BALLES) {
            Balle *b = &bg->balles[bg->nbBalles++];
            b->actif = 1;
            b->x     = j->muzzleX;
            b->y     = j->y + JOUEUR_H * 0.4f;
            b->velX  = (float)(j->direction * BULLET_SPEED);
        }
        return;
    }

    if (melee && (int)(now - j->lastMeleeTick) >= MELEE_COOLDOWN_MS) {
        j->lastMeleeTick = now;
        j->etat = PS_MELEE;
        animation_set_row(&j->anim, ANIM_P_MELEE,
                          PLAYER_NFRAMES[ANIM_P_MELEE],
                          PLAYER_ANIM_FPS[ANIM_P_MELEE], 0);

        /* Instant melee hit check */
        for (int i = 0; i < bg->nbEnnemis; i++) {
            Ennemi *e = &bg->ennemis[i];
            if (!e->actif || e->wagon != idx || e->etat == ES_DEAD) continue;
            float ex = e->x + (j->direction == 1 ? 0 : (float)JOUEUR_W);
            float dist = ex - (j->x + JOUEUR_W/2.0f);
            if (dist < 0) dist = -dist;
            if (dist < (float)MELEE_RANGE) {
                e->hp -= MELEE_DAMAGE;
                e->hitFlashMs = 150;
                hitfx_spawn(bg->hitFx, &bg->nbHitFx,
                            e->x + JOUEUR_W/2.0f, e->y + JOUEUR_H/3.0f,
                            bg->hitFxSheet);
                if (e->hp <= 0) {
                    e->etat = ES_DEAD;
                    int nf = (e->type == ET_ENFORCER) ? ENFORCER_NFRAMES[2] :
                             (e->type == ET_GUARD)    ? GUARD_NFRAMES[2] : WORKER_NFRAMES[2];
                    animation_set_row(&e->anim, ANIM_E_DEATH, nf,
                                      ENEMY_ANIM_FPS[ANIM_E_DEATH], 0);
                }
            }
        }
        return;
    }

    /* ── Movement ── */
    if (gauche)      { j->velX = -VITESSE_JOUEUR; j->direction = -1; }
    else if (droite) { j->velX =  VITESSE_JOUEUR; j->direction =  1; }
    else               j->velX *= 0.72f;

    if (sauter && j->auSol) { j->velY = JUMP_FORCE; j->auSol = 0; }

    j->velY += GRAVITE_JEU;
    if (j->velY > 18.0f) j->velY = 18.0f;
    j->x += j->velX;
    j->y += j->velY;

    /* Plafond */
    if (j->y < (float)(WAGON_PLAF_Y + 2)) { j->y = (float)(WAGON_PLAF_Y+2); j->velY = 0.0f; }

    /* Sol */
    j->auSol = 0;
    if (j->y + j->h >= (float)WAGON_SOL_Y) {
        j->y = (float)(WAGON_SOL_Y - j->h); j->velY = 0.0f; j->auSol = 1;
    }

    /* Obstacles */
    for (int i = 0; i < s->obstacles.nb; i++) {
        SDL_Rect pr = s->obstacles.rects[i];
        SDL_Rect jr = {(int)j->x, (int)j->y, j->w, j->h};
        if (j->velY > 0 && SDL_HasIntersection(&jr, &pr)) {
            if ((j->y - j->velY) + j->h <= (float)(pr.y + 6)) {
                j->y = (float)(pr.y - j->h); j->velY = 0.0f; j->auSol = 1;
            }
        }
    }

    /* ── Update animation state ── */
    if (!j->auSol) {
        EtatJoueur next = (j->velY < 0) ? PS_JUMP : PS_FALL;
        if (j->etat != next) {
            j->etat = next;
            int row = (next == PS_JUMP) ? ANIM_P_JUMP : ANIM_P_FALL;
            animation_set_row(&j->anim, row, PLAYER_NFRAMES[row], PLAYER_ANIM_FPS[row], 0);
        }
    } else if ((gauche || droite) && (j->etat != PS_WALK)) {
        j->etat = PS_WALK;
        animation_set_row(&j->anim, ANIM_P_WALK,
                          PLAYER_NFRAMES[ANIM_P_WALK], PLAYER_ANIM_FPS[ANIM_P_WALK], 1);
    } else if (!gauche && !droite && j->auSol && j->etat != PS_IDLE) {
        j->etat = PS_IDLE;
        animation_set_row(&j->anim, ANIM_P_IDLE,
                          PLAYER_NFRAMES[ANIM_P_IDLE], PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
    }

    /* ── Doors ── */
    if (j->x + j->w >= (float)(WAGON_MUR_D_X - WAGON_PORTE_W) && idx < MAX_WAGONS-1) {
        transitionSlide_declencher(bg, 1); return;
    }
    if (j->x <= (float)(WAGON_MUR_G_X + WAGON_PORTE_W) && idx > 0) {
        transitionSlide_declencher(bg, -1); return;
    }

    /* ── Wall stops ── */
    int leftStop  = WAGON_MUR_G_X + (idx > 0 ? WAGON_PORTE_W : 0);
    int rightStop = WAGON_MUR_D_X - (idx < MAX_WAGONS-1 ? WAGON_PORTE_W : 0) - j->w;
    if (j->x < (float)leftStop)  { j->x = (float)leftStop;  j->velX = 0.0f; }
    if (j->x > (float)rightStop) { j->x = (float)rightStop; j->velX = 0.0f; }
}

void joueur_afficher_interieur(Background *bg, SDL_Renderer *renderer, int ox)
{
    Joueur *j = &bg->joueur;
    if (!j->actif) return;

    /* Draw sprite sheet frame */
    if (bg->playerSheet) {
        SDL_Rect src = animation_get_src_rect(&j->anim);
        /* Center sprite over hitbox */
        int dx = (int)j->x + ox + JOUEUR_W/2 - PLAYER_DRAW_W/2;
        int dy = (int)j->y  + JOUEUR_H - PLAYER_DRAW_H;
        SDL_Rect dst = {dx, dy, PLAYER_DRAW_W, PLAYER_DRAW_H};
        SDL_RendererFlip flip = (j->direction == -1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderCopyEx(renderer, bg->playerSheet, &src, &dst, 0, NULL, flip);
    } else {
        /* Fallback: colored rectangles */
        SDL_Rect dest = {(int)j->x + ox, (int)j->y, j->w, j->h};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 44, 48, 56, 255);
        SDL_RenderFillRect(renderer, &dest);
        SDL_SetRenderDrawColor(renderer, 65, 105, 155, 255);
        SDL_Rect torso = {dest.x+2, dest.y+dest.h*3/10, dest.w-4, dest.h*4/10};
        SDL_RenderFillRect(renderer, &torso);
        SDL_SetRenderDrawColor(renderer, 205, 185, 165, 255);
        SDL_Rect tete = {dest.x+dest.w/4, dest.y-dest.h/5+2, dest.w/2, dest.h/5};
        SDL_RenderFillRect(renderer, &tete);
    }

    /* Muzzle flash */
    if (j->muzzleFrames > 0 && bg->muzzleTex) {
        int mx = (int)j->muzzleX + ox - MUZZLE_DRAW_W/2;
        if (j->direction == 1) mx = (int)j->x + ox + JOUEUR_W;
        else                   mx = (int)j->x + ox - MUZZLE_DRAW_W;
        int my = (int)j->muzzleY - MUZZLE_DRAW_H/2;
        SDL_Rect mdst = {mx, my, MUZZLE_DRAW_W, MUZZLE_DRAW_H};
        SDL_RendererFlip mflip = (j->direction == -1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderCopyEx(renderer, bg->muzzleTex, NULL, &mdst, 0, NULL, mflip);
    }
}

/* ═══════════════════════════════════════════════════
   TRANSITION GLISSEMENT
   ═══════════════════════════════════════════════════ */

void transitionSlide_declencher(Background *bg, int direction)
{
    if (bg->transitionSlide.actif) return;
    bg->transitionSlide.actif      = 1;
    bg->transitionSlide.direction  = direction;
    bg->transitionSlide.offset     = 0.0f;
    bg->transitionSlide.wagonCible = bg->wagonActif + direction;
}

void transitionSlide_update(Background *bg)
{
    if (!bg->transitionSlide.actif) return;
    bg->transitionSlide.offset += 30.0f;
    if (bg->transitionSlide.offset >= (float)W_ECRAN) {
        bg->transitionSlide.offset = 0.0f;
        bg->transitionSlide.actif  = 0;
        bg->wagonActif = bg->transitionSlide.wagonCible;
        /* Repositionner joueur à l'entrée du nouveau wagon */
        if (bg->transitionSlide.direction == 1)
            bg->joueur.x = (float)(WAGON_MUR_G_X + WAGON_PORTE_W + 6);
        else
            bg->joueur.x = (float)(WAGON_MUR_D_X - WAGON_PORTE_W - JOUEUR_W - 6);
        bg->joueur.y    = (float)(WAGON_SOL_Y - JOUEUR_H);
        bg->joueur.velX = bg->joueur.velY = 0.0f;
        bg->joueur.auSol = 1;
    }
}

/* ═══════════════════════════════════════════════════
   RENDU PRINCIPAL INTÉRIEUR
   ═══════════════════════════════════════════════════ */

void wagonsInterieurs_afficher(Background *bg, SDL_Renderer *renderer)
{
    int idx = bg->wagonActif;
    int dir = bg->transitionSlide.direction;
    int off = (int)bg->transitionSlide.offset;
    int nxt = bg->transitionSlide.wagonCible;

    if (!bg->transitionSlide.actif) {
        wagon_dessiner(bg, renderer, idx, 0);
        ennemis_afficher(bg->ennemis, bg->nbEnnemis, idx, renderer);
        balles_afficher(bg->balles, bg->nbBalles, renderer, bg->bulletTex);
        hitfx_afficher(bg->hitFx, bg->nbHitFx, renderer);
        joueur_afficher_interieur(bg, renderer, 0);
    } else {
        int ox_cur = (dir == 1) ? -off :  off;
        int ox_nxt = (dir == 1) ?  W_ECRAN - off : -W_ECRAN + off;
        wagon_dessiner(bg, renderer, nxt, ox_nxt);
        wagon_dessiner(bg, renderer, idx, ox_cur);
        joueur_afficher_interieur(bg, renderer, ox_cur);
    }

    /* ── Neige ── */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < MAX_NEIGE; i++) {
        ParticuleNeige *p = &bg->neige[i];
        SDL_SetRenderDrawColor(renderer, 200, 220, 255, (Uint8)p->alpha);
        SDL_Rect r = {(int)p->x, (int)p->y, p->taille, p->taille};
        SDL_RenderFillRect(renderer, &r);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* ── Player HP bar ── */
    if (bg->joueur.hp > 0) {
        int barW   = 120;
        int filled = (bg->joueur.hp * barW) / JOUEUR_MAX_HP;
        SDL_Rect bgBar = {14, H_ECRAN - 44, barW, 6};
        SDL_Rect fgBar = {14, H_ECRAN - 44, filled, 6};
        SDL_SetRenderDrawColor(renderer, 60, 20, 20, 255);
        SDL_RenderFillRect(renderer, &bgBar);
        SDL_SetRenderDrawColor(renderer, 220, 60, 60, 255);
        SDL_RenderFillRect(renderer, &fgBar);
    }
}

/* ═══════════════════════════════════════════════════
   MINIMAP
   ═══════════════════════════════════════════════════ */

void minimap_afficher(Background *bg, SDL_Renderer *renderer)
{
    const int MM_X = W_ECRAN - 170;
    const int MM_Y = 8;
    const int MM_W = 160;
    const int MM_H = 80;
    const int WX   = WAGON_MUR_D_X - WAGON_MUR_G_X;
    const int WY   = WAGON_SOL_Y   - WAGON_PLAF_Y;

    SDL_Rect box = {MM_X, MM_Y, MM_W, MM_H};

    /* semi-transparent dark fill */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 8, 14, 22, 180);
    SDL_RenderFillRect(renderer, &box);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* 1px orange border */
    SDL_SetRenderDrawColor(renderer, 230, 140, 50, 255);
    SDL_RenderDrawRect(renderer, &box);

    /* floor line */
    SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
    SDL_Rect floorLine = {MM_X + 1, MM_Y + MM_H - 3, MM_W - 2, 2};
    SDL_RenderFillRect(renderer, &floorLine);

    /* door pips — top-left and top-right corners */
    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_Rect ldoor = {MM_X + 2,          MM_Y + 2, 4, 4};
    SDL_Rect rdoor = {MM_X + MM_W - 6,   MM_Y + 2, 4, 4};
    SDL_RenderFillRect(renderer, &ldoor);
    SDL_RenderFillRect(renderer, &rdoor);

    /* enemies in current wagon — map feet + center-x */
    SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
    for (int i = 0; i < bg->nbEnnemis; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != bg->wagonActif || e->etat == ES_DEAD) continue;
        int ehw = (e->type == ET_GUARD) ? GUARD_HBW :
                  (e->type == ET_WORKER) ? WORKER_HBW : ENFORCER_HBW;
        int ehh = (e->type == ET_GUARD) ? GUARD_HBH :
                  (e->type == ET_WORKER) ? WORKER_HBH : ENFORCER_HBH;
        int ex = MM_X + (int)((e->x + ehw / 2.0f - WAGON_MUR_G_X) * (float)MM_W / WX);
        int ey = MM_Y + (int)((e->y + ehh         - WAGON_PLAF_Y)  * (float)MM_H / WY);
        if (ex < MM_X + 2)        ex = MM_X + 2;
        if (ex > MM_X + MM_W - 3) ex = MM_X + MM_W - 3;
        if (ey < MM_Y + 2)        ey = MM_Y + 2;
        if (ey > MM_Y + MM_H - 3) ey = MM_Y + MM_H - 3;
        SDL_Rect er = {ex - 1, ey - 1, 2, 2};
        SDL_RenderFillRect(renderer, &er);
    }

    /* player — map feet + center-x, drawn last so always on top */
    float pfeet_x = bg->joueur.x + JOUEUR_W / 2.0f;
    float pfeet_y = bg->joueur.y + JOUEUR_H;
    int px = MM_X + (int)((pfeet_x - WAGON_MUR_G_X) * (float)MM_W / WX);
    int py = MM_Y + (int)((pfeet_y - WAGON_PLAF_Y)  * (float)MM_H / WY);
    if (px < MM_X + 2)        px = MM_X + 2;
    if (px > MM_X + MM_W - 3) px = MM_X + MM_W - 3;
    if (py < MM_Y + 2)        py = MM_Y + 2;
    if (py > MM_Y + MM_H - 3) py = MM_Y + MM_H - 3;
    SDL_SetRenderDrawColor(renderer, 20, 40, 80, 255);
    SDL_Rect po = {px - 2, py - 2, 5, 5};
    SDL_RenderFillRect(renderer, &po);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect pr = {px - 1, py - 1, 3, 3};
    SDL_RenderFillRect(renderer, &pr);
}

/* ═══ LIBÉRATION ═══ */

void wagons_liberer(Background *bg)
{ for(int i=0;i<bg->nbWagons;i++) if(bg->wagons[i].texture) SDL_DestroyTexture(bg->wagons[i].texture); }

void background_liberer(Background *bg)
{
    if(bg->imagebackground)      SDL_DestroyTexture(bg->imagebackground);
    if(bg->imagebackground2)     SDL_DestroyTexture(bg->imagebackground2);
    if(bg->son)                  Mix_FreeMusic(bg->son);
    if(bg->fenetreguide.texture) SDL_DestroyTexture(bg->fenetreguide.texture);
    bg->imagebackground=bg->imagebackground2=NULL;
    bg->son=NULL; bg->fenetreguide.texture=NULL;
    for (int i = 0; i < MAX_WAGONS; i++) {
        if (bg->stylesWagons[i].floorTex)    SDL_DestroyTexture(bg->stylesWagons[i].floorTex);
        if (bg->stylesWagons[i].obstacleTex) SDL_DestroyTexture(bg->stylesWagons[i].obstacleTex);
        bg->stylesWagons[i].floorTex = bg->stylesWagons[i].obstacleTex = NULL;
        if (bg->wagonBgTex[i]) SDL_DestroyTexture(bg->wagonBgTex[i]);
        bg->wagonBgTex[i] = NULL;
    }
    if (bg->outsideBgTex) SDL_DestroyTexture(bg->outsideBgTex);
    if (bg->playerSheet)  SDL_DestroyTexture(bg->playerSheet);
    for (int i = 0; i < 3; i++) {
        if (bg->enemySheets[i]) SDL_DestroyTexture(bg->enemySheets[i]);
    }
    if (bg->bulletTex)  SDL_DestroyTexture(bg->bulletTex);
    if (bg->muzzleTex)  SDL_DestroyTexture(bg->muzzleTex);
    if (bg->hitFxSheet) SDL_DestroyTexture(bg->hitFxSheet);
    wagons_liberer(bg);
    destructibles_liberer(bg);
}
