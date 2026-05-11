/*
*
*   SUPER MARIO BROS  -  Enhanced Edition
*   Built with Raylib 
*
*
*   This build adds three production-grade systems on top of the original game
*   without altering any gameplay, level data, physics, drawing, or scoring:
*
*     1) PROCEDURAL AUDIO ENGINE  (audio_engine section)
*        - All sound effects and background music are SYNTHESIZED at startup
*          using raw PCM wave generation. No external audio files needed.
*        - 11 sound effects: jump, coin, q-block, stomp, die, level-clear,
*          game-over, save-chime, menu-move, menu-select, flag-get.
*        - 5 background music tracks: title theme, level 1 (overworld),
*          level 2 (desert variation), level 3 (minor-key night), win fanfare.
*        - Music transitions managed by a tiny BGM state machine with
*          seamless looping (re-trigger on completion).
*        - Volume hooks honor the existing Options screen toggles.
*
*     2) ROBUST SAVE / LOAD SYSTEM  (with file_handler section)
*        - Save file uses a 4-byte magic number ("MRBO") + version field +
*          timestamp + checksum so corrupt or foreign files are rejected
*          cleanly instead of silently breaking the menu.
*        - Load Game now WORKS: shows "NO SAVE FILE FOUND" toast on miss,
*          shows "LOADED!" + timestamp on success.
*        - F5 quick-saves anywhere during gameplay.
*        - Pause menu's SAVE GAME shows an animated confirmation toast.
*
*     3) TOAST NOTIFICATION SYSTEM
*        - Animated, fading on-screen messages for save/load feedback.
*        - Slides in from the top, holds, fades out.
*
*   Original gameplay, graphics, levels, collisions  and Enemy(Goombas)AI are preserved EXACTLY.
*
*   Original feature list:
*     - Main Menu (New Game / Load Game / Options / Quit)
*     - 3 Distinct Levels with unique stunning backgrounds
*     - Save / Load progress
*     - Options screen (Music / SFX toggle, Difficulty)
*     - Polished player, enemies, coins, particles
*     - Pause menu, Level Complete, Game Over screens
*
*******************************************************************************************/

#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define SW 960
#define SH 540
#define PW 26
#define PH 38
#define MAX_G 4
#define MAX_S 3
#define MAX_M 2
#define MAX_C 8
#define MAX_PLATS 8
#define MAX_PIPES 3
#define MAX_Q 3
#define MAX_PARTICLES 80
#define MAX_STARS 60
#define AUDIO_SR        44100               /* sample rate (Hz)            */
#define SAVE_MAGIC      0x4D52424F          /* 'MRBO' little-endian magic  */
#define SAVE_VERSION    2                   /* save format version         */
#define SAVE_FILE       "savegame.dat"
#define TOAST_MAX       96

/* SFX ids ->index into the sfx[] array */
enum {
    SFX_JUMP = 0, SFX_COIN, SFX_QBLOCK, SFX_STOMP,
    SFX_DIE,  SFX_CLEAR, SFX_GAMEOVER, SFX_SAVE,
    SFX_MENU_MOVE, SFX_MENU_SELECT, SFX_FLAG,
    SFX_COUNT
};

/* BGM ids -> index into the bgm[] array (-1 = silence) */
enum { BGM_NONE = -1, BGM_MENU = 0, BGM_LEVEL1, BGM_LEVEL2, BGM_LEVEL3, BGM_WIN, BGM_COUNT };
typedef enum {
    SCENE_MENU = 0,
    SCENE_OPTIONS,
    SCENE_LEVEL_SELECT,
    SCENE_GAME,
    SCENE_PAUSE,
    SCENE_LEVEL_COMPLETE,
    SCENE_GAME_OVER,
    SCENE_GAME_WIN
} Scene;

typedef struct {
    Rectangle r;
    float vx, vy;
    bool alive, squashed;
    int sqTimer;
    float pMin, pMax;
    float ix, iy, ivx;
} Goomba;

typedef struct {
    Rectangle r;
    float bx, by, amp, spd, ph;
    bool horiz;
    float prX, prY;
} MovP;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float life;
    float maxLife;
    Color color;
    float size;
    bool active;
} Particle;

typedef struct {
    bool musicOn;
    bool sfxOn;
    int  difficulty;
} Options;
typedef struct {
    unsigned int magic;        /* must equal SAVE_MAGIC                       */
    int          version;      /* must equal SAVE_VERSION                     */
    int          level;        /* current level 1..3                          */
    int          score;
    int          coins;
    int          lives;
    int          unlocked;     /* highest unlocked level                      */
    long long    timestamp;    /* unix epoch seconds when saved               */
    unsigned int checksum;     /* simple sum of fields for integrity          */
} SaveData;


typedef struct { float freq; float dur; } Note;   /* freq=0 -> rest */

/* GLOBALS  (originals + new audio/toast/save)*/
static Scene     scene     = SCENE_MENU;
static int       menuSel   = 0;
static int       optSel    = 0;
static int       lvlSel    = 0;
static Options   opt       = { true, true, 1 };
static SaveData  saveData  = { 0 };
static int       currentLevel = 1;
static int       unlocked  = 1;

static Particle  particles[MAX_PARTICLES];
static Vector2   stars[MAX_STARS];
static float     starTwinkle[MAX_STARS];

/* level data */
static Rectangle plats[MAX_PLATS]; static int plC = 0;
static Rectangle pipes[MAX_PIPES]; static int piC = 0;
static Rectangle qBox[MAX_Q];      static bool qHit[MAX_Q]; static int qC = 0;
static Rectangle cn[MAX_C];        static bool cnT[MAX_C];  static int cnC = 0;
static Goomba    gb[MAX_G];        static int gbC = 0;
static Rectangle sp[MAX_S];        static int spC = 0;
static MovP      mp[MAX_M];        static int mpC = 0;
static Rectangle goalFlag;

/* player */
static float px, py, vx, vy;
static bool  gnd, right_dir;
static int   score, coins, tLeft, lives, frame;
static int   playState;
static int   dTimer;
static int   onMP;
static const float SX_SPAWN = 80;
static const float SY_SPAWN = 380;

/* physics */
static const float G    = 0.55f;
static const float JMP  = -11.5f;
static const float BNC  = -7.5f;
static const float SPD  = 4.5f;
static const float MAXF = 13.0f;

/* NEW: audio engine state */
static Sound sfx[SFX_COUNT];
static Sound bgm[BGM_COUNT];
static int   currentBGM   = BGM_NONE;
static bool  audioReady   = false;

/* NEW: toast notifications */
static char  toastMsg[TOAST_MAX] = "";
static int   toastTimer          = 0;
static int   toastTotalTime      = 120;
static Color toastColor          = WHITE;

/* FORWARD DECLARATIONS*/
void LoadLevel(int lvl);
void DrawBackground(int lvl, int frame);
void SpawnParticle(Vector2 pos, Color c);
void UpdateParticles(void);
void DrawParticles(void);
void DrawPlayer(float x, float y, bool right, bool gnd, bool walk, int f);
void DrawDead(float x, float y, int t);
void DrawGoomba(Rectangle r, int f, bool sq);
void DrawSpike(Rectangle r);
void DrawQ(int x, int y, int s, bool hit, int f);
void ResetPlayer(void);
void InitStars(void);

/* file handler */
bool SaveGame(void);
bool LoadGameFile(void);
bool HasSaveFile(void);
const char *GetSaveSummary(void);

/* audio engine */
void InitAudioEngine(void);
void UnloadAudioEngine(void);
void PlaySfx(int id);
void SetBGM(int id);
void UpdateBGM(void);

/* toast */
void ShowToast(const char *msg, Color c, int durationFrames);
void UpdateToast(void);
void DrawToast(void);

/* AUDIO  ENGINE

   All audio is synthesized at startup as PCM wave data.
   No external audio assets are required -- the game compiles standalone.

   The melody builder takes a list of (frequency, duration) notes and emits
   an 8-bit-style square wave with short attack/release envelopes to avoid
   click artifacts.  Background music is generated the same way and played
   as long Sounds; the BGM update loop re-triggers playback when each track
   completes, producing seamless looping.
*/

/* core synth: build a Sound from a Note[] sequence  */
static Sound BuildMelody(const Note *notes, int count, int waveType, float volume)
{
    /* compute total length */
    float total = 0.0f;
    for (int i = 0; i < count; i++) total += notes[i].dur;
    int totalSamples = (int)(total * AUDIO_SR) + 1;

    short *data = (short *)calloc((size_t)totalSamples, sizeof(short));
    if (!data) {
        Sound empty = { 0 };
        return empty;
    }

    int idx = 0;
    for (int n = 0; n < count; n++) {
        int ns = (int)(notes[n].dur * AUDIO_SR);
        if (idx + ns > totalSamples) ns = totalSamples - idx;
        if (ns <= 0) continue;

        if (notes[n].freq > 0.5f) {
            int attack  = (int)(AUDIO_SR * 0.005f);
            int release = (int)(AUDIO_SR * 0.015f);
            if (attack  > ns / 3) attack  = ns / 3;
            if (release > ns / 3) release = ns / 3;

            for (int i = 0; i < ns; i++) {
                float t   = (float)i / (float)AUDIO_SR;
                float env = 1.0f;
                if (i < attack)              env = (float)i / (float)attack;
                else if (i > ns - release)   env = (float)(ns - i) / (float)release;

                float phase = 2.0f * PI * notes[n].freq * t;
                float v;
                if (waveType == 1) {                      /* square (8-bit) */
                    v = (sinf(phase) > 0.0f) ? 0.85f : -0.85f;
                } else if (waveType == 2) {               /* 25% pulse     */
                    float p = fmodf(phase / (2.0f * PI), 1.0f);
                    v = (p < 0.25f) ? 0.85f : -0.85f;
                } else {                                  /* sine          */
                    v = sinf(phase);
                }
                data[idx + i] = (short)(v * env * volume * 16000.0f);
            }
        }
        idx += ns;
    }

    Wave w;
    w.frameCount = (unsigned int)totalSamples;
    w.sampleRate = AUDIO_SR;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = data;

    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);                  /* frees malloc'd data buffer        */
    return s;
}

/*  short SFX  */
static Sound MakeJump(void) {
    Note n[] = {
        {523.25f, 0.04f}, {659.25f, 0.04f}, {783.99f, 0.04f},
        {1046.50f, 0.06f}, {1318.51f, 0.05f}
    };
    return BuildMelody(n, 5, 1, 0.30f);
}
static Sound MakeCoin(void) {
    Note n[] = { {987.77f, 0.06f}, {1318.51f, 0.10f} };           /* B5 -> E6 */
    return BuildMelody(n, 2, 1, 0.30f);
}
static Sound MakeQBlock(void) {
    Note n[] = { {659.25f, 0.04f}, {1046.50f, 0.08f} };
    return BuildMelody(n, 2, 1, 0.25f);
}
static Sound MakeStomp(void) {
    Note n[] = { {220.0f, 0.04f}, {130.0f, 0.05f}, {80.0f, 0.05f} };
    return BuildMelody(n, 3, 1, 0.40f);
}
static Sound MakeDie(void) {
    Note n[] = {
        {659.25f, 0.10f}, {0, 0.04f},
        {523.25f, 0.10f}, {0, 0.04f},
        {440.00f, 0.10f}, {0, 0.04f},
        {349.23f, 0.18f}, {0, 0.06f},
        {261.63f, 0.30f}
    };
    return BuildMelody(n, 9, 1, 0.30f);
}
static Sound MakeClear(void) {                             /* level-clear fanfare */
    Note n[] = {
        {392.00f, 0.10f}, {523.25f, 0.10f}, {659.25f, 0.10f},
        {783.99f, 0.10f}, {1046.50f, 0.18f}, {0, 0.04f},
        {1046.50f, 0.10f}, {0, 0.04f},
        {1046.50f, 0.10f}, {0, 0.04f},
        {1046.50f, 0.18f}, {1318.51f, 0.36f}
    };
    return BuildMelody(n, 12, 1, 0.30f);
}
static Sound MakeGameOver(void) {
    Note n[] = {
        {329.63f, 0.20f}, {0, 0.05f},
        {261.63f, 0.20f}, {0, 0.05f},
        {220.00f, 0.20f}, {0, 0.05f},
        {174.61f, 0.50f}
    };
    return BuildMelody(n, 7, 1, 0.30f);
}
static Sound MakeSave(void) {
    Note n[] = { {659.25f, 0.06f}, {783.99f, 0.06f}, {1046.50f, 0.12f} };
    return BuildMelody(n, 3, 1, 0.25f);
}
static Sound MakeMenuMove(void) {
    Note n[] = { {659.25f, 0.04f} };
    return BuildMelody(n, 1, 1, 0.18f);
}
static Sound MakeMenuSelect(void) {
    Note n[] = { {523.25f, 0.05f}, {1046.50f, 0.07f} };
    return BuildMelody(n, 2, 1, 0.22f);
}
static Sound MakeFlag(void) {
    Note n[] = {
        {523.25f, 0.10f}, {587.33f, 0.10f}, {659.25f, 0.10f},
        {698.46f, 0.10f}, {783.99f, 0.20f}
    };
    return BuildMelody(n, 5, 1, 0.30f);
}

/* BGM tracks */
static Sound MakeBGM_Menu(void) {
    /* Title theme: 8-bit Mario-style hook */
    Note n[] = {
        {659.25f, 0.12f}, {0, 0.04f}, {659.25f, 0.12f}, {0, 0.16f},
        {659.25f, 0.12f}, {0, 0.16f}, {523.25f, 0.12f}, {0, 0.04f},
        {659.25f, 0.12f}, {0, 0.16f}, {783.99f, 0.12f}, {0, 0.40f},
        {392.00f, 0.12f}, {0, 0.40f},

        {523.25f, 0.16f}, {0, 0.16f}, {392.00f, 0.16f}, {0, 0.16f},
        {329.63f, 0.16f}, {0, 0.32f},
        {440.00f, 0.16f}, {0, 0.04f}, {493.88f, 0.16f}, {0, 0.04f},
        {466.16f, 0.12f}, {440.00f, 0.20f}, {0, 0.30f},

        {0, 0.50f}    /* tail silence so loop has breathing room */
    };
    int c = sizeof(n)/sizeof(n[0]);
    return BuildMelody(n, c, 1, 0.16f);
}
static Sound MakeBGM_Level1(void) {
    /* Cheerful overworld, full hook + bridge */
    Note n[] = {
        {659.25f, 0.12f}, {0, 0.04f}, {659.25f, 0.12f}, {0, 0.16f},
        {659.25f, 0.12f}, {0, 0.16f}, {523.25f, 0.12f}, {0, 0.04f},
        {659.25f, 0.12f}, {0, 0.16f}, {783.99f, 0.12f}, {0, 0.40f},
        {392.00f, 0.12f}, {0, 0.40f},

        {523.25f, 0.16f}, {0, 0.16f}, {392.00f, 0.16f}, {0, 0.16f},
        {329.63f, 0.16f}, {0, 0.32f},
        {440.00f, 0.16f}, {0, 0.04f}, {493.88f, 0.16f}, {0, 0.04f},
        {466.16f, 0.12f}, {440.00f, 0.20f}, {0, 0.16f},

        {392.00f, 0.16f}, {659.25f, 0.16f}, {880.00f, 0.16f}, {0, 0.16f},
        {698.46f, 0.16f}, {783.99f, 0.16f}, {0, 0.16f},
        {659.25f, 0.16f}, {0, 0.04f},
        {523.25f, 0.16f}, {587.33f, 0.16f}, {493.88f, 0.16f}, {0, 0.40f},
        {0, 0.30f}
    };
    int c = sizeof(n)/sizeof(n[0]);
    return BuildMelody(n, c, 1, 0.16f);
}
static Sound MakeBGM_Level2(void) {
    /* Faster desert tempo, brighter */
    Note n[] = {
        {523.25f, 0.10f}, {659.25f, 0.10f}, {783.99f, 0.10f}, {0, 0.06f},
        {659.25f, 0.10f}, {523.25f, 0.10f}, {0, 0.10f},
        {587.33f, 0.10f}, {698.46f, 0.10f}, {880.00f, 0.10f}, {0, 0.06f},
        {698.46f, 0.10f}, {587.33f, 0.10f}, {0, 0.10f},

        {523.25f, 0.10f}, {659.25f, 0.10f}, {783.99f, 0.10f}, {1046.50f, 0.18f}, {0, 0.10f},
        {880.00f, 0.10f}, {783.99f, 0.10f}, {659.25f, 0.18f}, {0, 0.10f},
        {587.33f, 0.10f}, {523.25f, 0.20f}, {0, 0.30f}
    };
    int c = sizeof(n)/sizeof(n[0]);
    return BuildMelody(n, c, 1, 0.16f);
}
static Sound MakeBGM_Level3(void) {
    /* Mysterious minor-key night theme */
    Note n[] = {
        {261.63f, 0.20f}, {311.13f, 0.20f}, {349.23f, 0.20f}, {0, 0.10f},
        {311.13f, 0.20f}, {261.63f, 0.30f}, {0, 0.10f},
        {220.00f, 0.20f}, {261.63f, 0.20f}, {311.13f, 0.20f}, {0, 0.10f},
        {261.63f, 0.40f}, {0, 0.20f},

        {523.25f, 0.15f}, {622.25f, 0.15f}, {698.46f, 0.15f}, {0, 0.10f},
        {622.25f, 0.15f}, {523.25f, 0.30f}, {0, 0.30f}
    };
    int c = sizeof(n)/sizeof(n[0]);
    return BuildMelody(n, c, 2, 0.15f);
}
static Sound MakeBGM_Win(void) {
    /* Triumphant fanfare */
    Note n[] = {
        {523.25f, 0.12f}, {659.25f, 0.12f}, {783.99f, 0.12f}, {1046.50f, 0.20f},
        {0, 0.06f},
        {1046.50f, 0.10f}, {1318.51f, 0.10f}, {1046.50f, 0.10f}, {1318.51f, 0.30f},
        {0, 0.20f},
        {1046.50f, 0.12f}, {880.00f, 0.12f}, {783.99f, 0.12f}, {1046.50f, 0.40f},
        {0, 0.40f}
    };
    int c = sizeof(n)/sizeof(n[0]);
    return BuildMelody(n, c, 1, 0.20f);
}

/*  engine lifecycle / playback */
void InitAudioEngine(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) { audioReady = false; return; }
    audioReady = true;

    sfx[SFX_JUMP]        = MakeJump();
    sfx[SFX_COIN]        = MakeCoin();
    sfx[SFX_QBLOCK]      = MakeQBlock();
    sfx[SFX_STOMP]       = MakeStomp();
    sfx[SFX_DIE]         = MakeDie();
    sfx[SFX_CLEAR]       = MakeClear();
    sfx[SFX_GAMEOVER]    = MakeGameOver();
    sfx[SFX_SAVE]        = MakeSave();
    sfx[SFX_MENU_MOVE]   = MakeMenuMove();
    sfx[SFX_MENU_SELECT] = MakeMenuSelect();
    sfx[SFX_FLAG]        = MakeFlag();

    bgm[BGM_MENU]   = MakeBGM_Menu();
    bgm[BGM_LEVEL1] = MakeBGM_Level1();
    bgm[BGM_LEVEL2] = MakeBGM_Level2();
    bgm[BGM_LEVEL3] = MakeBGM_Level3();
    bgm[BGM_WIN]    = MakeBGM_Win();
}

void UnloadAudioEngine(void)
{
    if (!audioReady) return;
    for (int i = 0; i < SFX_COUNT; i++) UnloadSound(sfx[i]);
    for (int i = 0; i < BGM_COUNT; i++) UnloadSound(bgm[i]);
    CloseAudioDevice();
}

void PlaySfx(int id)
{
    if (!audioReady || !opt.sfxOn || id < 0 || id >= SFX_COUNT) return;
    /* stop any currently playing instance to avoid stacking on rapid fire */
    StopSound(sfx[id]);
    PlaySound(sfx[id]);
}

void SetBGM(int id)
{
    if (!audioReady) { currentBGM = id; return; }
    if (id == currentBGM) return;
    if (currentBGM != BGM_NONE) StopSound(bgm[currentBGM]);
    currentBGM = id;
    if (id != BGM_NONE && opt.musicOn) {
        SetSoundVolume(bgm[id], 1.0f);
        PlaySound(bgm[id]);
    }
}

/* Per-frame BGM handler: enforces opt.musicOn and re-triggers loops. */
void UpdateBGM(void)
{
    if (!audioReady || currentBGM == BGM_NONE) return;

    if (!opt.musicOn) {
        if (IsSoundPlaying(bgm[currentBGM])) StopSound(bgm[currentBGM]);
        return;
    }
    if (!IsSoundPlaying(bgm[currentBGM])) PlaySound(bgm[currentBGM]);
}

/*TOAST  NOTIFICATIONS*/
void ShowToast(const char *msg, Color c, int durationFrames)
{
    strncpy(toastMsg, msg, TOAST_MAX - 1);
    toastMsg[TOAST_MAX - 1] = '\0';
    toastTotalTime = durationFrames;
    toastTimer     = durationFrames;
    toastColor     = c;
}

void UpdateToast(void)
{
    if (toastTimer > 0) toastTimer--;
}

void DrawToast(void)
{
    if (toastTimer <= 0) return;

    /* slide in from top, hold, fade-out */
    float t = (float)toastTimer / (float)toastTotalTime;
    float alpha = 1.0f;
    int slide = 0;
    if (t > 0.85f)      slide = (int)((1.0f - t) / 0.15f * 0 - (1.0f - (1.0f - t) / 0.15f) * 60);
    else if (t < 0.20f) alpha = t / 0.20f;
    if (slide > 0) slide = 0;
    if (alpha < 0) alpha = 0;

    int fontSize = 26;
    int textW = MeasureText(toastMsg, fontSize);
    int boxW  = textW + 60;
    int boxH  = fontSize + 24;
    int boxX  = SW / 2 - boxW / 2;
    int boxY  = 60 + slide;

    Color bg = (Color){ 0, 0, 0, (unsigned char)(180 * alpha) };
    Color border = toastColor; border.a = (unsigned char)(255 * alpha);
    Color text = toastColor;   text.a   = (unsigned char)(255 * alpha);

    DrawRectangleRounded((Rectangle){ (float)boxX, (float)boxY, (float)boxW, (float)boxH }, 0.4f, 8, bg);
    DrawRectangleRoundedLines((Rectangle){ (float)boxX, (float)boxY, (float)boxW, (float)boxH }, 0.4f, 8, border);
    DrawText(toastMsg, SW/2 - textW/2, boxY + 12, fontSize, text);
}

/* FILE  HANDLER  (SAVE / LOAD)
*/
static unsigned int ComputeChecksum(const SaveData *s)
{
    /* Simple additive checksum over the data fields (excluding the
       checksum field itself).  Sufficient to detect corruption. */
    return (unsigned int)(s->magic + (unsigned int)s->version
         + (unsigned int)s->level + (unsigned int)s->score
         + (unsigned int)s->coins + (unsigned int)s->lives
         + (unsigned int)s->unlocked + (unsigned int)s->timestamp);
}

bool SaveGame(void)
{
    SaveData s;
    s.magic     = SAVE_MAGIC;
    s.version   = SAVE_VERSION;
    s.level     = currentLevel;
    s.score     = score;
    s.coins     = coins;
    s.lives     = lives;
    s.unlocked  = unlocked;
    s.timestamp = (long long)time(NULL);
    s.checksum  = ComputeChecksum(&s);

    FILE *f = fopen(SAVE_FILE, "wb");
    if (!f) return false;
    size_t written = fwrite(&s, sizeof(SaveData), 1, f);
    fclose(f);
    if (written != 1) return false;

    saveData = s;
    return true;
}

bool LoadGameFile(void)
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return false;

    SaveData s;
    size_t got = fread(&s, sizeof(SaveData), 1, f);
    fclose(f);
    if (got != 1) return false;

    /* validate magic + version */
    if (s.magic != SAVE_MAGIC) return false;
    if (s.version != SAVE_VERSION) return false;

    /* validate checksum */
    unsigned int expected = s.checksum;
    s.checksum = 0;                      /* zero out for recompute */
    unsigned int actual = ComputeChecksum(&s);
    s.checksum = expected;
    if (actual != expected) return false;

    /* range-check fields */
    if (s.level    < 1 || s.level    > 3) return false;
    if (s.unlocked < 1 || s.unlocked > 3) return false;
    if (s.lives    < 0 || s.lives    > 99) return false;
    if (s.score    < 0) return false;
    if (s.coins    < 0) return false;

    saveData = s;
    return true;
}

bool HasSaveFile(void)
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

const char *GetSaveSummary(void)
{
    static char buf[96];
    if (saveData.magic != SAVE_MAGIC) {
        snprintf(buf, sizeof(buf), "No valid save");
        return buf;
    }
    time_t tt = (time_t)saveData.timestamp;
    struct tm *lt = localtime(&tt);
    char ts[32] = "unknown";
    if (lt) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", lt);
    snprintf(buf, sizeof(buf), "L%d - %d pts - %d coins  [%s]",
             saveData.level, saveData.score, saveData.coins, ts);
    return buf;
}

/*PARTICLE SYSTEM*/
void SpawnParticle(Vector2 pos, Color c) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) {
            particles[i].active = true;
            particles[i].pos = pos;
            particles[i].vel = (Vector2){ (float)(GetRandomValue(-30,30))/10.0f,
                                          (float)(GetRandomValue(-50,-10))/10.0f };
            particles[i].life = particles[i].maxLife = 35.0f;
            particles[i].color = c;
            particles[i].size = (float)GetRandomValue(2,5);
            return;
        }
    }
}

void UpdateParticles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        particles[i].pos.x += particles[i].vel.x;
        particles[i].pos.y += particles[i].vel.y;
        particles[i].vel.y += 0.25f;
        particles[i].life -= 1.0f;
        if (particles[i].life <= 0) particles[i].active = false;
    }
}

void DrawParticles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;
        float a = particles[i].life / particles[i].maxLife;
        Color c = particles[i].color;
        c.a = (unsigned char)(255 * a);
        DrawCircleV(particles[i].pos, particles[i].size * a, c);
    }
}

void InitStars(void) {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = (float)GetRandomValue(0, SW);
        stars[i].y = (float)GetRandomValue(0, SH/2);
        starTwinkle[i] = (float)GetRandomValue(0, 100) / 10.0f;
    }
}

/* DRAWING HELPERS*/
void DrawPlayer(float x, float y, bool right, bool g, bool walk, int f) {
    DrawEllipse((int)(x+PW/2), (int)(y+PH+2), PW/2, 4, (Color){0,0,0,80});
    DrawRectangleRounded((Rectangle){x+2, y+14, PW-4, PH-26}, 0.3f, 6, (Color){200,30,30,255});
    DrawRectangleRounded((Rectangle){x+4, y+18, PW-8, PH-30}, 0.3f, 6, (Color){40,80,200,255});
    DrawRectangleRec((Rectangle){x+4, y+PH-12, PW-8, 12}, (Color){30,40,120,255});
    DrawCircle((int)(x+PW/2-3), (int)(y+22), 1.5f, YELLOW);
    DrawCircle((int)(x+PW/2+3), (int)(y+22), 1.5f, YELLOW);
    int fo = (g && walk) ? (((f/5)%2) ? 3 : -3) : 0;
    DrawEllipse((int)(x+8+fo), (int)(y+PH), 6, 4, (Color){50,30,15,255});
    DrawEllipse((int)(x+PW-8-fo), (int)(y+PH), 6, 4, (Color){50,30,15,255});
    DrawCircle((int)(x+PW/2), (int)(y+8), 12, (Color){255,210,170,255});
    DrawEllipse((int)(x+PW/2), (int)(y+3), 14, 7, (Color){200,30,30,255});
    DrawEllipse((int)(x+PW/2 + (right?5:-5)), (int)(y+5), 8, 4, (Color){200,30,30,255});
    int d = right ? 1 : -1;
    DrawCircle((int)(x+PW/2+d*3-2), (int)(y+9), 2, WHITE);
    DrawCircle((int)(x+PW/2+d*3+2), (int)(y+9), 2, WHITE);
    DrawCircle((int)(x+PW/2+d*3-2), (int)(y+9), 1, BLACK);
    DrawCircle((int)(x+PW/2+d*3+2), (int)(y+9), 1, BLACK);
    DrawEllipse((int)(x+PW/2+d*3), (int)(y+13), 5, 2, (Color){60,30,10,255});
}

void DrawDead(float x, float y, int t) {
    float w = sinf(t*0.4f)*3;
    DrawRectangleRounded((Rectangle){x+2+w, y+14, PW-4, PH-26}, 0.3f, 6, (Color){200,30,30,255});
    DrawRectangleRounded((Rectangle){x+4+w, y+18, PW-8, PH-30}, 0.3f, 6, (Color){40,80,200,255});
    DrawCircle((int)(x+PW/2+w), (int)(y+8), 12, (Color){255,210,170,255});
    DrawEllipse((int)(x+PW/2+w), (int)(y+3), 14, 7, (Color){200,30,30,255});
    DrawText("X", (int)(x+PW/2+w-7), (int)(y+5), 12, BLACK);
    DrawText("X", (int)(x+PW/2+w+1), (int)(y+5), 12, BLACK);
}

void DrawGoomba(Rectangle r, int f, bool sq) {
    DrawEllipse((int)(r.x+r.width/2), (int)(r.y+r.height+2), r.width/2, 3, (Color){0,0,0,80});
    if (sq) {
        DrawEllipse((int)(r.x+r.width/2), (int)(r.y+r.height-3), r.width/2, 4, (Color){120,70,30,255});
        return;
    }
    DrawEllipse((int)(r.x+r.width/2), (int)(r.y+r.height/2+2), r.width/2+1, r.height/2+2, (Color){90,50,20,255});
    DrawEllipse((int)(r.x+r.width/2), (int)(r.y+r.height/2), r.width/2-1, r.height/2, (Color){170,100,50,255});
    DrawEllipse((int)(r.x+r.width/2), (int)(r.y+r.height/2-3), r.width/2-3, 3, (Color){200,140,80,255});
    DrawCircle((int)(r.x+r.width/2-5), (int)(r.y+10), 3, WHITE);
    DrawCircle((int)(r.x+r.width/2+5), (int)(r.y+10), 3, WHITE);
    DrawCircle((int)(r.x+r.width/2-5), (int)(r.y+10), 1.5f, BLACK);
    DrawCircle((int)(r.x+r.width/2+5), (int)(r.y+10), 1.5f, BLACK);
    DrawLineEx((Vector2){r.x+r.width/2-8, r.y+6}, (Vector2){r.x+r.width/2-2, r.y+8}, 2, BLACK);
    DrawLineEx((Vector2){r.x+r.width/2+2, r.y+8}, (Vector2){r.x+r.width/2+8, r.y+6}, 2, BLACK);
    int s = (f/8)%2;
    DrawEllipse((int)(r.x+5+(s?0:2)), (int)(r.y+r.height-1), 4, 3, BLACK);
    DrawEllipse((int)(r.x+r.width-5-(s?2:0)), (int)(r.y+r.height-1), 4, 3, BLACK);
}

void DrawSpike(Rectangle r) {
    int n = (int)(r.width/10);
    if (n<1) n=1;
    float sw = r.width/n;
    for (int i=0; i<n; i++) {
        float sx = r.x+i*sw;
        DrawTriangle((Vector2){sx, r.y+r.height},
                     (Vector2){sx+sw, r.y+r.height},
                     (Vector2){sx+sw/2, r.y}, (Color){200,200,220,255});
        DrawTriangleLines((Vector2){sx, r.y+r.height},
                     (Vector2){sx+sw, r.y+r.height},
                     (Vector2){sx+sw/2, r.y}, (Color){80,80,100,255});
    }
}

void DrawQ(int x, int y, int s, bool hit, int f) {
    if (hit) {
        DrawRectangle(x, y, s, s, (Color){140,95,40,255});
        DrawRectangleLines(x, y, s, s, (Color){80,50,10,255});
    } else {
        int by = y + (int)(sinf(f*0.08f)*2);
        DrawRectangleGradientV(x, by, s, s, (Color){255,200,30,255}, (Color){200,140,10,255});
        DrawText("?", x+s/2-7, by+5, 24, WHITE);
        DrawRectangleLines(x, by, s, s, (Color){80,50,10,255});
    }
}

/*BACKGROUNDS*/
void DrawBackground(int lvl, int f) {
    if (lvl == 1) {
        DrawRectangleGradientV(0, 0, SW, SH, (Color){120,200,255,255}, (Color){200,240,255,255});
        DrawCircle(SW-100, 90, 45, (Color){255,240,180,200});
        DrawCircle(SW-100, 90, 32, (Color){255,235,120,255});
        DrawTriangle((Vector2){0,300},(Vector2){200,150},(Vector2){400,300},(Color){120,140,180,255});
        DrawTriangle((Vector2){250,300},(Vector2){450,170},(Vector2){650,300},(Color){140,160,200,255});
        DrawTriangle((Vector2){500,300},(Vector2){750,140},(Vector2){960,300},(Color){120,140,180,255});
        float off = (float)((f/2) % 200);
        for (int i = -1; i < 6; i++)
            DrawCircle((int)(i*200 - off + 100), 460, 110, (Color){80,180,80,255});
        for (int i = -1; i < 8; i++)
            DrawCircle((int)(i*150 - off*1.5f + 75), 490, 90, (Color){60,160,60,255});
        for (int i = 0; i < 4; i++) {
            float cx = fmodf(i*250 + f*0.3f, SW + 200) - 100;
            DrawCircle((int)cx, 80+i*20, 22, WHITE);
            DrawCircle((int)cx+25, 75+i*20, 28, WHITE);
            DrawCircle((int)cx+55, 80+i*20, 22, WHITE);
        }
    }
    else if (lvl == 2) {
        DrawRectangleGradientV(0, 0, SW, SH/2, (Color){255,140,60,255}, (Color){255,200,100,255});
        DrawRectangleGradientV(0, SH/2, SW, SH/2, (Color){240,180,90,255}, (Color){200,140,60,255});
        DrawCircle(SW/2, 200, 80, (Color){255,220,120,180});
        DrawCircle(SW/2, 200, 60, (Color){255,180,80,255});
        DrawTriangle((Vector2){100,400},(Vector2){250,200},(Vector2){400,400},(Color){180,130,70,255});
        DrawTriangle((Vector2){250,200},(Vector2){400,400},(Vector2){330,400},(Color){140,90,50,255});
        DrawTriangle((Vector2){500,400},(Vector2){650,250},(Vector2){800,400},(Color){180,130,70,255});
        DrawTriangle((Vector2){650,250},(Vector2){800,400},(Vector2){730,400},(Color){140,90,50,255});
        for (int i = -1; i < 7; i++) DrawEllipse(i*180+90, 480, 140, 60, (Color){220,170,90,255});
        for (int i = -1; i < 9; i++) DrawEllipse(i*130+65, 510, 110, 50, (Color){200,150,75,255});
        for (int i = 0; i < 8; i++) {
            int x = (int)((i*120 + f) % SW);
            DrawPixel(x, 250 + (int)(sinf(f*0.05f+i)*5), (Color){255,255,255,150});
        }
    }
    else {
        DrawRectangleGradientV(0, 0, SW, SH, (Color){10,15,50,255}, (Color){50,30,90,255});
        for (int i = 0; i < MAX_STARS; i++) {
            float t = sinf(f*0.05f + starTwinkle[i]);
            unsigned char a = (unsigned char)(180 + 75*t);
            DrawCircle((int)stars[i].x, (int)stars[i].y, 1.2f + 0.5f*t, (Color){255,255,255,a});
        }
        DrawCircle(SW-120, 100, 50, (Color){240,240,220,80});
        DrawCircle(SW-120, 100, 40, (Color){250,250,230,255});
        DrawCircle(SW-110, 95, 8, (Color){200,200,180,255});
        DrawCircle(SW-130, 110, 6, (Color){200,200,180,255});
        DrawTriangle((Vector2){0,400},(Vector2){180,180},(Vector2){360,400},(Color){40,40,80,255});
        DrawTriangle((Vector2){180,180},(Vector2){240,260},(Vector2){200,260},(Color){240,240,255,255});
        DrawTriangle((Vector2){280,400},(Vector2){500,140},(Vector2){720,400},(Color){30,30,70,255});
        DrawTriangle((Vector2){500,140},(Vector2){560,230},(Vector2){520,230},(Color){240,240,255,255});
        DrawTriangle((Vector2){600,400},(Vector2){820,200},(Vector2){960,400},(Color){40,40,80,255});
        DrawTriangle((Vector2){820,200},(Vector2){870,270},(Vector2){830,270},(Color){240,240,255,255});
        for (int i = -1; i < 7; i++)
            DrawCircle(i*180+90, 490, 120, (Color){25,25,55,255});
    }
    if (lvl == 1) DrawRectangle(0, SH-50, SW, 50, (Color){90,55,30,255});
    else if (lvl == 2) DrawRectangle(0, SH-50, SW, 50, (Color){160,110,50,255});
    else DrawRectangle(0, SH-50, SW, 50, (Color){25,25,50,255});
}

/*LEVEL LOADING*/
void LoadLevel(int lvl) {
    currentLevel = lvl;
    plC = piC = qC = cnC = gbC = spC = mpC = 0;
    for (int i=0;i<MAX_Q;i++) qHit[i]=false;
    for (int i=0;i<MAX_C;i++) cnT[i]=false;

    if (lvl == 1) {
        plats[plC++] = (Rectangle){0, 460, 380, 80};
        plats[plC++] = (Rectangle){430, 460, 530, 80};
        plats[plC++] = (Rectangle){260, 360, 140, 18};
        plats[plC++] = (Rectangle){500, 310, 140, 18};
        plats[plC++] = (Rectangle){730, 360, 140, 18};

        pipes[piC++] = (Rectangle){650, 400, 50, 60};

        qBox[qC++] = (Rectangle){320, 360, 32, 32};
        qBox[qC++] = (Rectangle){555, 310, 32, 32};

        cn[cnC++] = (Rectangle){290, 330, 16, 16};
        cn[cnC++] = (Rectangle){530, 280, 16, 16};
        cn[cnC++] = (Rectangle){570, 280, 16, 16};
        cn[cnC++] = (Rectangle){760, 330, 16, 16};
        cn[cnC++] = (Rectangle){800, 330, 16, 16};

        gb[gbC++] = (Goomba){{300,436,26,24}, -0.7f, 0, true, false, 0, 100, 380, 300, 436, -0.7f};
        gb[gbC++] = (Goomba){{750,436,26,24},  0.7f, 0, true, false, 0, 500, 920, 750, 436,  0.7f};
        sp[spC++] = (Rectangle){400, 448, 30, 12};

        goalFlag = (Rectangle){910, 340, 12, 120};
    }
    else if (lvl == 2) {
        plats[plC++] = (Rectangle){0, 460, 280, 80};
        plats[plC++] = (Rectangle){340, 460, 280, 80};
        plats[plC++] = (Rectangle){680, 460, 280, 80};
        plats[plC++] = (Rectangle){200, 370, 130, 18};
        plats[plC++] = (Rectangle){430, 320, 140, 18};
        plats[plC++] = (Rectangle){650, 370, 130, 18};

        pipes[piC++] = (Rectangle){500, 400, 50, 60};

        qBox[qC++] = (Rectangle){250, 370, 32, 32};
        qBox[qC++] = (Rectangle){480, 320, 32, 32};

        cn[cnC++] = (Rectangle){235, 340, 16, 16};
        cn[cnC++] = (Rectangle){465, 290, 16, 16};
        cn[cnC++] = (Rectangle){505, 290, 16, 16};
        cn[cnC++] = (Rectangle){690, 340, 16, 16};
        cn[cnC++] = (Rectangle){730, 340, 16, 16};
        cn[cnC++] = (Rectangle){770, 340, 16, 16};

        gb[gbC++] = (Goomba){{200,436,26,24}, -0.9f, 0, true, false, 0, 50, 270, 200, 436, -0.9f};
        gb[gbC++] = (Goomba){{800,436,26,24},  0.9f, 0, true, false, 0, 690, 940, 800, 436, 0.9f};
        gb[gbC++] = (Goomba){{460,296,26,24},  0.5f, 0, true, false, 0, 432, 542, 460, 296, 0.5f};

        sp[spC++] = (Rectangle){290, 448, 40, 12};
        sp[spC++] = (Rectangle){630, 448, 40, 12};

        mp[mpC++] = (MovP){{780,260,70,14}, 780, 260, 60, 0.025f, 0.0f, true, 780, 260};

        goalFlag = (Rectangle){910, 340, 12, 120};
    }
    else {
        plats[plC++] = (Rectangle){0, 460, 220, 80};
        plats[plC++] = (Rectangle){280, 460, 200, 80};
        plats[plC++] = (Rectangle){540, 460, 200, 80};
        plats[plC++] = (Rectangle){800, 460, 160, 80};
        plats[plC++] = (Rectangle){170, 360, 110, 16};
        plats[plC++] = (Rectangle){390, 290, 130, 16};
        plats[plC++] = (Rectangle){610, 360, 110, 16};
        plats[plC++] = (Rectangle){800, 280, 130, 16};

        pipes[piC++] = (Rectangle){340, 400, 50, 60};
        pipes[piC++] = (Rectangle){740, 400, 50, 60};

        qBox[qC++] = (Rectangle){430, 290, 32, 32};
        qBox[qC++] = (Rectangle){200, 360, 32, 32};
        qBox[qC++] = (Rectangle){840, 280, 32, 32};

        cn[cnC++] = (Rectangle){420, 250, 16, 16};
        cn[cnC++] = (Rectangle){460, 250, 16, 16};
        cn[cnC++] = (Rectangle){640, 330, 16, 16};
        cn[cnC++] = (Rectangle){680, 330, 16, 16};
        cn[cnC++] = (Rectangle){830, 240, 16, 16};
        cn[cnC++] = (Rectangle){870, 240, 16, 16};
        cn[cnC++] = (Rectangle){100, 420, 16, 16};
        cn[cnC++] = (Rectangle){890, 420, 16, 16};

        gb[gbC++] = (Goomba){{150,436,26,24}, -1.0f, 0, true, false, 0, 30, 210, 150, 436, -1.0f};
        gb[gbC++] = (Goomba){{600,436,26,24},  1.0f, 0, true, false, 0, 540, 730, 600, 436, 1.0f};
        gb[gbC++] = (Goomba){{870,436,26,24}, -0.8f, 0, true, false, 0, 800, 950, 870, 436, -0.8f};
        gb[gbC++] = (Goomba){{420,266,26,24},  0.5f, 0, true, false, 0, 392, 502, 420, 266, 0.5f};

        sp[spC++] = (Rectangle){240, 448, 40, 12};
        sp[spC++] = (Rectangle){500, 448, 40, 12};
        sp[spC++] = (Rectangle){770, 448, 30, 12};

        mp[mpC++] = (MovP){{280,210,70,14}, 280, 210, 70, 0.028f, 0.0f, true, 280, 210};
        mp[mpC++] = (MovP){{700,200,70,14}, 700, 200, 60, 0.030f, 1.5f, false, 700, 200};

        goalFlag = (Rectangle){910, 340, 12, 120};
    }

    ResetPlayer();
    tLeft = 300;
    frame = 0;
    playState = 0; dTimer = 0;

    if (opt.difficulty == 0) tLeft = 400;
    else if (opt.difficulty == 2) tLeft = 220;

    /* NEW: switch BGM track per level */
    if (lvl == 1) SetBGM(BGM_LEVEL1);
    else if (lvl == 2) SetBGM(BGM_LEVEL2);
    else SetBGM(BGM_LEVEL3);
}

void ResetPlayer(void) {
    px = SX_SPAWN; py = SY_SPAWN;
    vx = vy = 0;
    gnd = false;
    right_dir = true;
    onMP = -1;
}

/* GAME UPDATE(PlaySfx hooks added) */
void UpdateGame(void) {
    frame++;
    if (frame % 60 == 0 && tLeft > 0) tLeft--;
    if (tLeft <= 0 && playState == 0) {
        playState = 1; dTimer = 0; vy = -10;
        PlaySfx(SFX_DIE);                                  /* AUDIO HOOK */
    }

    bool walk = false;

    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) {
        scene = SCENE_PAUSE;
        menuSel = 0;
        PlaySfx(SFX_MENU_SELECT);                          /* AUDIO HOOK */
        return;
    }

    /* NEW: F5 quick-save during gameplay */
    if (IsKeyPressed(KEY_F5) && playState == 0) {
        if (SaveGame()) {
            ShowToast("QUICK-SAVED!", (Color){80,255,140,255}, 110);
            PlaySfx(SFX_SAVE);
        } else {
            ShowToast("SAVE FAILED!", (Color){255,100,100,255}, 110);
        }
    }

    if (playState == 1) {
        vy += G;
        if (vy > MAXF) vy = MAXF;
        py += vy;
        dTimer++;
        if (dTimer >= 75) {
            lives--;
            if (lives < 0) {
                scene = SCENE_GAME_OVER;
                SetBGM(BGM_NONE);
                PlaySfx(SFX_GAMEOVER);                     /* AUDIO HOOK */
                return;
            }
            ResetPlayer();
            for (int i=0;i<gbC;i++) {
                gb[i].r.x=gb[i].ix; gb[i].r.y=gb[i].iy;
                gb[i].vx=gb[i].ivx; gb[i].vy=0;
                gb[i].alive=true; gb[i].squashed=false; gb[i].sqTimer=0;
            }
            playState = 0; dTimer = 0;
            tLeft = (opt.difficulty==0)?400:(opt.difficulty==2)?220:300;
        }
        return;
    }

    vx = 0;
    if (IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_D)) { vx = SPD; right_dir = true; walk = true; }
    if (IsKeyDown(KEY_LEFT)||IsKeyDown(KEY_A))  { vx = -SPD; right_dir = false; walk = true; }
    if ((IsKeyPressed(KEY_SPACE)||IsKeyPressed(KEY_UP)||IsKeyPressed(KEY_W)) && gnd) {
        vy = JMP; gnd = false;
        PlaySfx(SFX_JUMP);                                 /* AUDIO HOOK */
        for (int k=0;k<4;k++) SpawnParticle((Vector2){px+PW/2, py+PH}, (Color){255,255,255,255});
    }

    for (int i=0;i<mpC;i++) {
        mp[i].prX = mp[i].r.x;
        mp[i].prY = mp[i].r.y;
        float t = sinf(frame*mp[i].spd + mp[i].ph);
        if (mp[i].horiz) mp[i].r.x = mp[i].bx + t*mp[i].amp;
        else mp[i].r.y = mp[i].by + t*mp[i].amp;
    }

    if (onMP >= 0 && onMP < mpC) {
        px += mp[onMP].r.x - mp[onMP].prX;
        py += mp[onMP].r.y - mp[onMP].prY;
    }
    onMP = -1;

    vy += G;
    if (vy > MAXF) vy = MAXF;

    px += vx;
    Rectangle pr = {px, py, PW, PH};
    for (int i=0;i<plC;i++) {
        if (CheckCollisionRecs(pr, plats[i])) {
            if (vx > 0) px = plats[i].x - PW;
            else if (vx < 0) px = plats[i].x + plats[i].width;
            pr.x = px;
        }
    }
    for (int i=0;i<piC;i++) {
        Rectangle pp = {pipes[i].x-5, pipes[i].y, pipes[i].width+10, pipes[i].height};
        if (CheckCollisionRecs(pr, pp)) {
            if (vx > 0) px = pp.x - PW;
            else if (vx < 0) px = pp.x + pp.width;
            pr.x = px;
        }
    }
    for (int i=0;i<mpC;i++) {
        if (CheckCollisionRecs(pr, mp[i].r)) {
            if (vx > 0) px = mp[i].r.x - PW;
            else if (vx < 0) px = mp[i].r.x + mp[i].r.width;
            pr.x = px;
        }
    }

    py += vy;
    gnd = false;
    pr.x = px; pr.y = py;
    for (int i=0;i<plC;i++) {
        if (CheckCollisionRecs(pr, plats[i])) {
            if (vy > 0) { py = plats[i].y - PH; vy = 0; gnd = true; }
            else if (vy < 0) { py = plats[i].y + plats[i].height; vy = 1; }
            pr.y = py;
        }
    }
    for (int i=0;i<piC;i++) {
        Rectangle pp = {pipes[i].x-5, pipes[i].y, pipes[i].width+10, pipes[i].height};
        if (CheckCollisionRecs(pr, pp)) {
            if (vy > 0) { py = pp.y - PH; vy = 0; gnd = true; }
            else if (vy < 0) { py = pp.y + pp.height; vy = 1; }
            pr.y = py;
        }
    }
    for (int i=0;i<mpC;i++) {
        if (CheckCollisionRecs(pr, mp[i].r)) {
            if (vy > 0) { py = mp[i].r.y - PH; vy = 0; gnd = true; onMP = i; }
            else if (vy < 0) { py = mp[i].r.y + mp[i].r.height; vy = 1; }
            pr.y = py;
        }
    }

    for (int i=0;i<qC;i++) {
        if (!qHit[i]) {
            if (CheckCollisionRecs(pr, qBox[i]) && vy < 0) {
                qHit[i]=true; coins++; score+=200;
                vy = 1; py = qBox[i].y + qBox[i].height; pr.y = py;
                PlaySfx(SFX_QBLOCK);                       /* AUDIO HOOK */
                for (int k=0;k<6;k++) SpawnParticle((Vector2){qBox[i].x+16, qBox[i].y}, YELLOW);
            }
        }
    }

    for (int i=0;i<gbC;i++) {
        Goomba *g = &gb[i];
        if (!g->alive) continue;
        if (g->squashed) {
            g->sqTimer++;
            if (g->sqTimer > 30) g->alive = false;
            continue;
        }
        g->r.x += g->vx;
        if (g->r.x < g->pMin) { g->r.x = g->pMin; g->vx = -g->vx; }
        if (g->r.x > g->pMax) { g->r.x = g->pMax; g->vx = -g->vx; }
        g->vy += G;
        if (g->vy > MAXF) g->vy = MAXF;
        g->r.y += g->vy;
        for (int j=0;j<plC;j++) {
            if (CheckCollisionRecs(g->r, plats[j]) && g->vy > 0) {
                g->r.y = plats[j].y - g->r.height;
                g->vy = 0;
            }
        }
        Rectangle pl = {px, py, PW, PH};
        if (CheckCollisionRecs(pl, g->r)) {
            if (vy > 0 && (py + PH) <= g->r.y + 14) {
                g->squashed = true; g->sqTimer = 0;
                vy = BNC; score += 100;
                PlaySfx(SFX_STOMP);                        /* AUDIO HOOK */
                for (int k=0;k<8;k++) SpawnParticle((Vector2){g->r.x+13, g->r.y+12}, (Color){170,100,50,255});
            } else {
                playState = 1; dTimer = 0; vy = -10;
                PlaySfx(SFX_DIE);                          /* AUDIO HOOK */
                break;
            }
        }
    }

    if (playState == 0) {
        Rectangle pl = {px, py, PW, PH};
        for (int i=0;i<spC;i++) {
            if (CheckCollisionRecs(pl, sp[i])) {
                playState = 1; dTimer = 0; vy = -10;
                PlaySfx(SFX_DIE);                          /* AUDIO HOOK */
                break;
            }
        }
    }

    for (int i=0;i<cnC;i++) {
        if (!cnT[i]) {
            Rectangle pl = {px, py, PW, PH};
            if (CheckCollisionRecs(pl, cn[i])) {
                cnT[i] = true; coins++; score += 100;
                PlaySfx(SFX_COIN);                         /* AUDIO HOOK */
                for (int k=0;k<6;k++) SpawnParticle((Vector2){cn[i].x+8, cn[i].y+8}, YELLOW);
            }
        }
    }

    {
        Rectangle pl = {px, py, PW, PH};
        if (CheckCollisionRecs(pl, goalFlag)) {
            score += 1000 + tLeft*10;
            if (currentLevel >= unlocked && currentLevel < 3) unlocked = currentLevel + 1;
            SaveGame();
            ShowToast("PROGRESS SAVED!", (Color){80,255,140,255}, 90);
            PlaySfx(SFX_FLAG);                             /* AUDIO HOOK */
            if (currentLevel == 3) {
                scene = SCENE_GAME_WIN;
                SetBGM(BGM_WIN);
            } else {
                scene = SCENE_LEVEL_COMPLETE;
                SetBGM(BGM_NONE);
                PlaySfx(SFX_CLEAR);                        /* AUDIO HOOK */
            }
            return;
        }
    }

    if (py > SH+20) {
        playState = 1; dTimer = 0; vy = -10;
        PlaySfx(SFX_DIE);                                  /* AUDIO HOOK */
    }
    if (px < 0) px = 0;
    if (px > SW - PW) px = SW - PW;

    UpdateParticles();
    (void)walk; /* original code retained walk variable */
}

/*GAME DRAWING*/
void DrawGameScene(void) {
    DrawBackground(currentLevel, frame);

    for (int i=0;i<plC;i++) {
        Color top, body, line;
        if (currentLevel == 1) { top=(Color){50,200,50,255}; body=(Color){150,90,40,255}; line=(Color){80,50,20,255}; }
        else if (currentLevel == 2) { top=(Color){230,180,90,255}; body=(Color){180,120,60,255}; line=(Color){100,70,30,255}; }
        else { top=(Color){200,200,230,255}; body=(Color){70,70,100,255}; line=(Color){30,30,60,255}; }
        DrawRectangleRec(plats[i], body);
        DrawRectangle((int)plats[i].x, (int)plats[i].y, (int)plats[i].width, 8, top);
        DrawRectangleLines((int)plats[i].x, (int)plats[i].y, (int)plats[i].width, (int)plats[i].height, line);
    }

    for (int i=0;i<piC;i++) {
        Color body = (currentLevel==3)?(Color){80,80,140,255}:(Color){0,160,0,255};
        Color rim  = (currentLevel==3)?(Color){120,120,180,255}:(Color){0,200,0,255};
        Color outl = (currentLevel==3)?(Color){30,30,60,255}:(Color){0,90,0,255};
        DrawRectangleRec((Rectangle){pipes[i].x, pipes[i].y+14, pipes[i].width, pipes[i].height-14}, body);
        DrawRectangleRec((Rectangle){pipes[i].x-5, pipes[i].y, pipes[i].width+10, 16}, rim);
        DrawRectangleLines((int)pipes[i].x, (int)(pipes[i].y+14), (int)pipes[i].width, (int)(pipes[i].height-14), outl);
        DrawRectangleLines((int)(pipes[i].x-5), (int)pipes[i].y, (int)(pipes[i].width+10), 16, outl);
    }

    for (int i=0;i<qC;i++) DrawQ((int)qBox[i].x, (int)qBox[i].y, 32, qHit[i], frame);

    for (int i=0;i<cnC;i++) {
        if (!cnT[i]) {
            float b = sinf(frame*0.08f + i*1.3f)*3;
            float scale = 0.8f + 0.2f*sinf(frame*0.1f+i);
            DrawCircle((int)(cn[i].x+8), (int)(cn[i].y+8+b), 8*scale, (Color){255,210,40,255});
            DrawCircle((int)(cn[i].x+8), (int)(cn[i].y+8+b), 6*scale, (Color){255,240,100,255});
            DrawText("$", (int)(cn[i].x+5), (int)(cn[i].y+3+b), 12, (Color){180,130,0,255});
        }
    }

    for (int i=0;i<mpC;i++) {
        DrawRectangleRounded(mp[i].r, 0.3f, 6, (Color){200,170,100,255});
        DrawRectangleRoundedLines(mp[i].r, 0.3f, 6, (Color){120,90,40,255});
        DrawCircle((int)mp[i].r.x+6, (int)(mp[i].r.y+mp[i].r.height/2), 2, (Color){80,60,30,255});
        DrawCircle((int)(mp[i].r.x+mp[i].r.width-6), (int)(mp[i].r.y+mp[i].r.height/2), 2, (Color){80,60,30,255});
    }

    for (int i=0;i<spC;i++) DrawSpike(sp[i]);

    for (int i=0;i<gbC;i++) if (gb[i].alive) DrawGoomba(gb[i].r, frame, gb[i].squashed);

    DrawRectangle((int)goalFlag.x, (int)goalFlag.y, 4, (int)goalFlag.height, (Color){80,80,80,255});
    float wave = sinf(frame*0.1f)*3;
    DrawTriangle((Vector2){goalFlag.x+4, goalFlag.y+5},
                 (Vector2){goalFlag.x+50+wave, goalFlag.y+20},
                 (Vector2){goalFlag.x+4, goalFlag.y+35},
                 (Color){220,40,40,255});
    DrawText("GOAL", (int)goalFlag.x+8, (int)goalFlag.y+12, 12, WHITE);

    DrawParticles();

    if (playState == 1) DrawDead(px, py, dTimer);
    else {
        bool walking = (IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_LEFT)||IsKeyDown(KEY_A)||IsKeyDown(KEY_D));
        DrawPlayer(px, py, right_dir, gnd, walking && gnd, frame);
    }

    DrawRectangleGradientV(0, 0, SW, 36, (Color){0,0,0,200}, (Color){0,0,0,120});
    DrawText(TextFormat("LIVES %d", lives), 12, 10, 18, WHITE);
    DrawText(TextFormat("COINS %d", coins), 140, 10, 18, YELLOW);
    DrawText(TextFormat("SCORE %06d", score), 290, 10, 18, WHITE);
    DrawText(TextFormat("TIME %d", tLeft), 510, 10, 18, tLeft<=30?RED:WHITE);
    DrawText(TextFormat("LEVEL %d-1", currentLevel), 660, 10, 18, (Color){255,200,80,255});
    DrawText("F5=Save  ESC=Pause", SW-180, 12, 14, LIGHTGRAY);

    DrawToast();
}

/*MENU / SCREEN DRAWING*/
void DrawMenuBackground(int f) {
    DrawRectangleGradientV(0, 0, SW, SH, (Color){30,40,90,255}, (Color){90,40,120,255});
    for (int i = 0; i < 12; i++) {
        float x = fmodf(i*120 + f*0.5f, SW + 80) - 40;
        float y = 80 + 40*sinf(f*0.02f + i);
        DrawCircle((int)x, (int)y, 20 + 5*sinf(f*0.03f+i), (Color){255,255,255,30});
    }
    for (int i = 0; i < MAX_STARS; i++) {
        float t = sinf(f*0.05f + starTwinkle[i]);
        unsigned char a = (unsigned char)(120 + 100*t);
        DrawCircle((int)stars[i].x, (int)stars[i].y, 1.5f, (Color){255,255,255,a});
    }
}

void DrawMainMenu(void) {
    DrawMenuBackground(frame);

    const char *title = "SUPER MARIO BROS";
    int tw = MeasureText(title, 56);
    DrawText(title, SW/2 - tw/2 + 3, 83, 56, (Color){0,0,0,150});
    DrawText(title, SW/2 - tw/2, 80, 56, (Color){255,220,80,255});

    const char *sub = "STRUCTURED PROGAMMING PROJECT";
    int sw = MeasureText(sub, 32);
    DrawText(sub, SW/2 - sw/2, 145, 32, (Color){255,255,255,220});

    const char *items[] = { "NEW GAME", "LOAD GAME", "OPTIONS", "QUIT" };
    bool hasSave = HasSaveFile();

    for (int i = 0; i < 4; i++) {
        int fontSize = (i == menuSel) ? 38 : 30;
        Color c = (i == menuSel) ? (Color){255,220,80,255} : WHITE;
        if (i == 1 && !hasSave) c = (i == menuSel) ? (Color){255,160,80,255} : (Color){180,180,180,255};
        const char *it = items[i];
        int w = MeasureText(it, fontSize);
        int y = 240 + i*55;
        if (i == menuSel) {
            DrawRectangleRounded((Rectangle){SW/2 - w/2 - 30, y-5, w+60, fontSize+10}, 0.3f, 8, (Color){255,220,80,40});
            DrawText(">", SW/2 - w/2 - 30, y, fontSize, c);
            DrawText("<", SW/2 + w/2 + 12, y, fontSize, c);
        }
        DrawText(it, SW/2 - w/2, y, fontSize, c);
    }

    /* show save info under LOAD GAME if hovering on it */
    if (menuSel == 1 && hasSave) {
        const char *sum = GetSaveSummary();
        int sw2 = MeasureText(sum, 16);
        DrawText(sum, SW/2 - sw2/2, 295, 16, (Color){200,255,200,200});
    } else if (menuSel == 1 && !hasSave) {
        const char *m = "(no save file yet -- play and reach a flag, or press F5)";
        int sw2 = MeasureText(m, 16);
        DrawText(m, SW/2 - sw2/2, 295, 16, (Color){255,180,180,200});
    }

    DrawText("UP/DOWN: navigate  |  ENTER: select  |  L: level select", 230, SH-30, 16, LIGHTGRAY);

    DrawToast();
}

void DrawOptions(void) {
    DrawMenuBackground(frame);
    const char *title = "OPTIONS";
    int tw = MeasureText(title, 50);
    DrawText(title, SW/2 - tw/2, 80, 50, (Color){255,220,80,255});

    const char *labels[] = { "MUSIC", "SOUND FX", "DIFFICULTY", "BACK" };
    char buf[64];
    for (int i = 0; i < 4; i++) {
        int fs = (i == optSel) ? 32 : 26;
        Color c = (i == optSel) ? (Color){255,220,80,255} : WHITE;
        if (i == 0) snprintf(buf, sizeof(buf), "MUSIC      <  %s  >", opt.musicOn ? "ON" : "OFF");
        else if (i == 1) snprintf(buf, sizeof(buf), "SOUND FX   <  %s  >", opt.sfxOn ? "ON" : "OFF");
        else if (i == 2) {
            const char *d = opt.difficulty==0?"EASY":(opt.difficulty==1?"NORMAL":"HARD");
            snprintf(buf, sizeof(buf), "DIFFICULTY <  %s  >", d);
        }
        else snprintf(buf, sizeof(buf), "%s", labels[i]);
        int w = MeasureText(buf, fs);
        DrawText(buf, SW/2 - w/2, 200 + i*55, fs, c);
    }
    DrawText("LEFT/RIGHT to change, ENTER on BACK to return", 230, SH-30, 16, LIGHTGRAY);
    DrawToast();
}

void DrawLevelSelect(void) {
    DrawMenuBackground(frame);
    const char *title = "SELECT LEVEL";
    int tw = MeasureText(title, 50);
    DrawText(title, SW/2 - tw/2, 70, 50, (Color){255,220,80,255});

    const char *names[] = { "1 - GRASSLANDS", "2 - DESERT SUNSET", "3 - NIGHT MOUNTAINS" };
    Color colors[] = { (Color){90,200,90,255}, (Color){255,160,80,255}, (Color){140,140,255,255} };
    for (int i = 0; i < 3; i++) {
        bool locked = (i+1) > unlocked;
        int fs = (i == lvlSel) ? 34 : 28;
        Color c = locked ? GRAY : (i == lvlSel ? (Color){255,220,80,255} : WHITE);
        int w = MeasureText(names[i], fs);
        int y = 200 + i*70;
        DrawRectangleRounded((Rectangle){SW/2 - 200, y-10, 400, 50}, 0.3f, 8, (Color){colors[i].r, colors[i].g, colors[i].b, locked?60:120});
        DrawText(names[i], SW/2 - w/2, y, fs, c);
        if (locked) DrawText("[LOCKED]", SW/2 + w/2 + 10, y+5, 18, RED);
    }
    DrawText("UP/DOWN select, ENTER play, ESC back", 290, SH-30, 16, LIGHTGRAY);
    DrawToast();
}

void DrawPauseScreen(void) {
    DrawGameScene();
    DrawRectangle(0, 0, SW, SH, (Color){0,0,0,160});
    const char *t = "PAUSED";
    int w = MeasureText(t, 60);
    DrawText(t, SW/2 - w/2, 160, 60, WHITE);
    const char *items[] = { "RESUME", "SAVE GAME", "MAIN MENU" };
    for (int i = 0; i < 3; i++) {
        int fs = (i == menuSel) ? 32 : 26;
        Color c = (i == menuSel) ? (Color){255,220,80,255} : WHITE;
        int iw = MeasureText(items[i], fs);
        DrawText(items[i], SW/2 - iw/2, 260 + i*50, fs, c);
    }
    DrawToast();
}

void DrawLevelComplete(void) {
    DrawMenuBackground(frame);
    const char *t = "LEVEL COMPLETE!";
    int w = MeasureText(t, 56);
    DrawText(t, SW/2 - w/2, 130, 56, (Color){255,220,80,255});
    DrawText(TextFormat("SCORE: %06d", score), SW/2 - 100, 230, 28, WHITE);
    DrawText(TextFormat("COINS: %d", coins), SW/2 - 60, 270, 28, YELLOW);
    DrawText(TextFormat("TIME BONUS: %d", tLeft*10), SW/2 - 110, 310, 28, (Color){180,255,180,255});
    DrawText("Press ENTER for Next Level", SW/2 - 170, 400, 22, WHITE);
    DrawText("Press M for Main Menu", SW/2 - 130, 430, 20, LIGHTGRAY);
    DrawToast();
}

void DrawGameOver(void) {
    DrawMenuBackground(frame);
    const char *t = "GAME OVER";
    int w = MeasureText(t, 64);
    DrawText(t, SW/2 - w/2 + 3, 153, 64, (Color){0,0,0,180});
    DrawText(t, SW/2 - w/2, 150, 64, (Color){255,80,80,255});
    DrawText(TextFormat("FINAL SCORE: %06d", score), SW/2 - 130, 270, 24, WHITE);
    DrawText("Press ENTER for Main Menu", SW/2 - 165, 380, 22, WHITE);
}

void DrawGameWin(void) {
    DrawMenuBackground(frame);
    const char *t = "YOU WIN!";
    int w = MeasureText(t, 70);
    DrawText(t, SW/2 - w/2, 130, 70, (Color){255,220,80,255});
    const char *s = "All 3 worlds conquered!";
    int sw = MeasureText(s, 28);
    DrawText(s, SW/2 - sw/2, 220, 28, WHITE);
    DrawText(TextFormat("FINAL SCORE: %06d", score), SW/2 - 130, 290, 24, YELLOW);
    DrawText(TextFormat("COINS COLLECTED: %d", coins), SW/2 - 130, 330, 24, YELLOW);
    DrawText("Press ENTER for Main Menu", SW/2 - 165, 420, 22, WHITE);

    for (int i = 0; i < 30; i++) {
        float t1 = fmodf(frame*2 + i*30, SH+100);
        Color c = (i%3==0)?RED:((i%3==1)?YELLOW:GREEN);
        DrawRectangle((int)((i*73)%SW), (int)t1, 4, 8, c);
    }
}

/*MENU LOGIC (with audio + load-game;in our second project submission we were asked to have the load game option functionable and some bgm as per the original mario game)*/
void UpdateMainMenu(void) {
    if (IsKeyPressed(KEY_DOWN)) { menuSel = (menuSel+1)%4; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_UP))   { menuSel = (menuSel+3)%4; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_ENTER)) {
        PlaySfx(SFX_MENU_SELECT);
        if (menuSel == 0) {
            score = 0; coins = 0; lives = 5; unlocked = 1;
            currentLevel = 1;
            LoadLevel(1);
            scene = SCENE_GAME;
        } else if (menuSel == 1) {
            /*FIXED: load game now reports clear feedback */
            if (LoadGameFile()) {
                score = saveData.score;
                coins = saveData.coins;
                lives = saveData.lives;
                unlocked = saveData.unlocked;
                currentLevel = saveData.level;
                LoadLevel(currentLevel);
                scene = SCENE_GAME;
                ShowToast(TextFormat("LOADED LEVEL %d", currentLevel),
                          (Color){80,255,140,255}, 110);
            } else {
                /* graceful failure with on-screen message */
                ShowToast("NO VALID SAVE FILE FOUND",
                          (Color){255,140,80,255}, 130);
            }
        } else if (menuSel == 2) {
            scene = SCENE_OPTIONS; optSel = 0;
        } else {
            CloseWindow();
            exit(0);
        }
    }
    if (IsKeyPressed(KEY_L)) {
        scene = SCENE_LEVEL_SELECT; lvlSel = 0;
        PlaySfx(SFX_MENU_SELECT);
    }
}

void UpdateOptions(void) {
    bool prevMusic = opt.musicOn;
    if (IsKeyPressed(KEY_DOWN)) { optSel = (optSel+1)%4; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_UP))   { optSel = (optSel+3)%4; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT)) {
        if (optSel == 0) opt.musicOn = !opt.musicOn;
        else if (optSel == 1) opt.sfxOn = !opt.sfxOn;
        else if (optSel == 2) {
            if (IsKeyPressed(KEY_LEFT)) opt.difficulty = (opt.difficulty+2)%3;
            else opt.difficulty = (opt.difficulty+1)%3;
        }
        PlaySfx(SFX_MENU_MOVE);
    }
    if (opt.musicOn != prevMusic) {
        if (!opt.musicOn && currentBGM != BGM_NONE) StopSound(bgm[currentBGM]);
        if (opt.musicOn && currentBGM != BGM_NONE) PlaySound(bgm[currentBGM]);
    }
    if (IsKeyPressed(KEY_ENTER) && optSel == 3) { scene = SCENE_MENU; menuSel = 2; PlaySfx(SFX_MENU_SELECT); }
    if (IsKeyPressed(KEY_ESCAPE)) { scene = SCENE_MENU; menuSel = 2; PlaySfx(SFX_MENU_SELECT); }
}

void UpdateLevelSelect(void) {
    if (IsKeyPressed(KEY_DOWN)) { lvlSel = (lvlSel+1)%3; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_UP))   { lvlSel = (lvlSel+2)%3; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_ENTER)) {
        if ((lvlSel+1) <= unlocked) {
            score = 0; coins = 0; lives = 5;
            currentLevel = lvlSel+1;
            LoadLevel(currentLevel);
            scene = SCENE_GAME;
            PlaySfx(SFX_MENU_SELECT);
        } else {
            ShowToast("LEVEL LOCKED", (Color){255,140,80,255}, 80);
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) { scene = SCENE_MENU; PlaySfx(SFX_MENU_SELECT); }
}

void UpdatePause(void) {
    if (IsKeyPressed(KEY_DOWN)) { menuSel = (menuSel+1)%3; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_UP))   { menuSel = (menuSel+2)%3; PlaySfx(SFX_MENU_MOVE); }
    if (IsKeyPressed(KEY_ENTER)) {
        if (menuSel == 0) {
            scene = SCENE_GAME;
            PlaySfx(SFX_MENU_SELECT);
        }
        else if (menuSel == 1) {
            if (SaveGame()) {
                ShowToast("GAME SAVED!", (Color){80,255,140,255}, 110);
                PlaySfx(SFX_SAVE);
            } else {
                ShowToast("SAVE FAILED!", (Color){255,100,100,255}, 110);
            }
        }
        else {
            scene = SCENE_MENU;
            menuSel = 0;
            SetBGM(BGM_MENU);
            PlaySfx(SFX_MENU_SELECT);
        }
    }
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) {
        scene = SCENE_GAME;
        PlaySfx(SFX_MENU_SELECT);
    }
}

/* MAIN */
int main(void) {
    InitWindow(SW, SH, "Super Mario Bros");
    SetTargetFPS(60);
    SetExitKey(0);

    /* audio engine on */
    InitAudioEngine();

    InitStars();
    for (int i=0;i<MAX_PARTICLES;i++) particles[i].active = false;

    score = 0; coins = 0; lives = 5; tLeft = 300; frame = 0;
    playState = 0; dTimer = 0;

    /* attempt to peek save file so the menu can show the summary */
    LoadGameFile();

    /* start menu music */
    SetBGM(BGM_MENU);

    while (!WindowShouldClose()) {
        frame++;

        /*UPDATE*/
        switch (scene) {
            case SCENE_MENU:           UpdateMainMenu();    break;
            case SCENE_OPTIONS:        UpdateOptions();     break;
            case SCENE_LEVEL_SELECT:   UpdateLevelSelect(); break;
            case SCENE_GAME:           UpdateGame();        break;
            case SCENE_PAUSE:          UpdatePause();       break;
            case SCENE_LEVEL_COMPLETE:
                if (IsKeyPressed(KEY_ENTER)) {
                    if (currentLevel < 3) {
                        currentLevel++;
                        LoadLevel(currentLevel);
                        scene = SCENE_GAME;
                    } else {
                        scene = SCENE_MENU; menuSel = 0;
                        SetBGM(BGM_MENU);
                    }
                    PlaySfx(SFX_MENU_SELECT);
                }
                if (IsKeyPressed(KEY_M)) {
                    scene = SCENE_MENU; menuSel = 0;
                    SetBGM(BGM_MENU);
                    PlaySfx(SFX_MENU_SELECT);
                }
                break;
            case SCENE_GAME_OVER:
                if (IsKeyPressed(KEY_ENTER)) {
                    scene = SCENE_MENU; menuSel = 0;
                    SetBGM(BGM_MENU);
                    PlaySfx(SFX_MENU_SELECT);
                }
                break;
            case SCENE_GAME_WIN:
                if (IsKeyPressed(KEY_ENTER)) {
                    scene = SCENE_MENU; menuSel = 0;
                    SetBGM(BGM_MENU);
                    PlaySfx(SFX_MENU_SELECT);
                }
                break;
        }

        /* keep BGM looping & honor music toggle */
        UpdateBGM();
        UpdateToast();

        /* ---- DRAW ---- */
        BeginDrawing();
        ClearBackground(BLACK);

        switch (scene) {
            case SCENE_MENU:           DrawMainMenu();      break;
            case SCENE_OPTIONS:        DrawOptions();       break;
            case SCENE_LEVEL_SELECT:   DrawLevelSelect();   break;
            case SCENE_GAME:           DrawGameScene();     break;
            case SCENE_PAUSE:          DrawPauseScreen();   break;
            case SCENE_LEVEL_COMPLETE: DrawLevelComplete(); break;
            case SCENE_GAME_OVER:      DrawGameOver();      break;
            case SCENE_GAME_WIN:       DrawGameWin();       break;
        }

        EndDrawing();
    }

    /* NEW:clean shutdown */
    UnloadAudioEngine();
    CloseWindow();
    return 0;
}
