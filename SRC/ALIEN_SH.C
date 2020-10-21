#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include <io.h>
#include <time.h>

#define sgn(x) ((x < 0) ? -1 : ((x > 0) ? 1 : 0))
#define INPUT_STATUS_0 0x3da
#define ESC 27
#define PALETTE_INDEX 0x03c8
#define PALETTE_DATA 0x03c9
#define NUM_SCAN_QUE 256
#define KEY_UP_MASK 128
#define MAX_COLOR_NUM 256
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define SCREEN_SIZE 64000u
#define TICKS (*(volatile dword far *)(0x0040006CL))

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned long dword;
typedef short sword;

typedef struct
{
    word width;
    word height;
    byte palette[256 * 3];
    byte* data;
} BITMAP;

typedef struct
{
    int x, y, dx, dy, width, height;
    int maxX, maxY;
    int oldX, oldY;
    byte far* data, * erase;
    int reverse;
    byte visible, untouchable, lives;
} SPRITE;

typedef struct
{
    char name[9];
    int points;
} RECORD;

byte far* screen;
byte far* offScreen;
byte palette[256][4];
int oldMode;
int charHeight;
int charWidth;
byte far* FontPtr;
byte far* F8x8Ptr;
byte far* F8x14Ptr;
byte scanKeyCode, scanKeyCodeEx;
byte scanKeyCodeQueque[NUM_SCAN_QUE];
byte scanKeyCodeQuequeHead;
byte scanKeyCodeQuequeTail;
RECORD* ranking;

void EnterMode0x13(void);
void LeaveMode0x13(void);
void UpdateBuffer(void);
void DrawPixel(int, int, int);
int GetPixel(int, int);
void HorizontalLine(int, int, int, int);
void VerticalLine(int, int, int, int);
void Rectangle(int, int, int, int, int);
void RectangleFilled(int, int, int, int, int);
void Line(int, int, int, int, int);
void LoadBmp(char*, BITMAP*);
void FileSkip(FILE*, int);
void DrawBitmap(BITMAP*, int, int);
void DrawTransparentBitmap(BITMAP*, int, int, int);
void SetPalette(byte*);
void RotatePalette(byte*);
void TextInit(void);
void SetFont(int);
void DrawChar(int, int, int, int);
void DrawString(char*, int, int, int);
void GetSprite(byte far*, int, int, int, int);
void BlitSprite(byte far*, int, int);
void AnimateBmp(void);
dword GetTick(void);
static void interrupt(far* oldkb)(void);
void InitKeyboard(void);
void DeinitKeyboard(void);
void interrupt GetScan(void);
int ChceckCollision(SPRITE*, SPRITE*, int, int, int, int);
volatile unsigned long fastTick, slowTick;
static void interrupt(far* oldtimer)(void);
void InitTimer(void);
void DeinitTimer(void);
void LoadToSprite(BITMAP*, SPRITE*);
void DrawTransparentSprite(SPRITE*);
void GetSpriteFromSprite(byte far*, byte far*, int, int, int, int);
void GameOver(byte far*, int);
int ReadFromFileToStruc(RECORD*);
int WriteFromStrucToFile(RECORD*);
void DrawRandomBg(void);
int DetectAnyKeyPress(void);

void main()
{
    BITMAP bmp;
    byte far* eraseButton;

    LoadBmp("testbg.BMP", &bmp);
    TextInit();
    SetFont(0);
    InitKeyboard();

    InitVideoMode();
    SetPalette(bmp.palette);

    DrawRandomBg();
    DrawTransparentBitmap(&bmp, 0, 0, 0);
    free(bmp.data);

    LoadBmp("start.bmp", &bmp);
    if ((eraseButton = malloc(bmp.width * bmp.height + 4)) == NULL)
    {
        free(bmp.data);
        free(bmp.palette);
        printf("Error alloc sprite\n");
        exit(-1);
    }

    if ((ranking = malloc(sizeof(RECORD) * 10)) == NULL)
    {
        free(eraseButton);
        free(bmp.data);
        free(bmp.palette);
        printf("Error alloc ranking\n");
        exit(-1);
    }

    ReadFromFileToStruc(ranking);

    GetSprite(eraseButton, 20, 30, bmp.width, bmp.height);
    scanKeyCodeEx = scanKeyCode;
    while (DetectAnyKeyPress() != 0)
    {
        DrawTransparentBitmap(&bmp, 20, 30, 0);
        UpdateBuffer();
        sleep(1);
        BlitSprite(eraseButton, 20, 30);
        UpdateBuffer();
        sleep(1);
    }
    free(bmp.data);
    free(bmp.palette);
    free(eraseButton);

    memset(offScreen, 0, SCREEN_SIZE);

    AnimateBmp();
    LeaveMode0x13();
    DeinitKeyboard();
}

int InitVideoMode(void)
{
    offScreen = malloc(64000u);
    if (offScreen)
    {
        screen = MK_FP(0xa000, 0);
        EnterMode0x13();
        memset(offScreen, 0, SCREEN_SIZE);
        return 0;
    }
    else
    {
        LeaveMode0x13();
        printf("Not enought mem for screen buffer\n");
        return 1;
    }
}

void EnterMode0x13(void)
{
    union REGS in, out;

    in.h.ah = 0xf;
    int86(0x10, &in, &out);
    oldMode = out.h.al;

    in.h.ah = 0;
    in.h.al = 0x13;
    int86(0x10, &in, &out);
}

void LeaveMode0x13(void)
{
    union REGS in, out;

    in.h.ah = 0;
    in.h.al = oldMode;
    int86(0x10, &in, &out);
}

void UpdateBuffer(void)
{
    while (inportb(INPUT_STATUS_0) & 8)
        ;
    while (!(inportb(INPUT_STATUS_0) & 8))
        ;
    memcpy(screen, offScreen, SCREEN_SIZE);
}
void FileSkip(FILE* fp, int numBytes)
{
    while (numBytes--)
        fgetc(fp);
}
void LoadBmp(char* file, BITMAP* b)
{
    FILE* fp;
    long index;
    word num_colors;
    int x, c;

    /* open the file */
    if ((fp = fopen(file, "rb")) == NULL)
    {
        printf("Error opening file %s.\n", file);
        exit(1);
    }

    /* check to see if it is a valid bitmap file */
    if (fgetc(fp) != 'B' || fgetc(fp) != 'M')
    {
        fclose(fp);
        printf("%s is not a bitmap file.\n", file);
        exit(1);
    }

    /* read in the width and height of the image, and the
     number of colors used; ignore the rest */
    FileSkip(fp, 16);
    fread(&b->width, sizeof(word), 1, fp);
    FileSkip(fp, 2);
    fread(&b->height, sizeof(word), 1, fp);
    FileSkip(fp, 22);
    fread(&num_colors, sizeof(word), 1, fp);
    FileSkip(fp, 6);

    /* assume we are working with an 8-bit file */
    if (num_colors == 0)
        num_colors = 256;

    /* try to allocate memory */
    if ((b->data = (byte*)malloc((word)(b->width * b->height))) == NULL)
    {
        fclose(fp);
        printf("Error allocating memory for file %s.\n", file);
        exit(1);
    }

    for (index = 0; index < num_colors; index++)
    {
        b->palette[(int)(index * 3 + 2)] = fgetc(fp) >> 2;
        b->palette[(int)(index * 3 + 1)] = fgetc(fp) >> 2;
        b->palette[(int)(index * 3 + 0)] = fgetc(fp) >> 2;
        x = fgetc(fp);
    }

    /* read the bitmap */
    for (index = (b->height - 1) * b->width; index >= 0; index -= b->width)
        for (x = 0; x < b->width; x++)
            b->data[(word)index + x] = (byte)fgetc(fp);

    fclose(fp);
}
void DrawTransparentBitmap(BITMAP* bmp, int x, int y, int reverse)
{
    int i, j;
    word screen_offset = (y << 8) + (y << 6);
    word bitmap_offset = 0;
    byte data;

    for (j = 0; j < bmp->height; j++)
    {
        for (i = 0; i < bmp->width; i++, bitmap_offset++)
        {
            data = bmp->data[bitmap_offset];
            if (data)
                if (reverse)
                    offScreen[screen_offset + x + bmp->width - 1 - i] = data;
                else
                    offScreen[screen_offset + x + i] = data;
        }
        screen_offset += SCREEN_WIDTH;
    }
}
void SetPalette(byte* palette)
{
    int i;

    outp(PALETTE_INDEX, 0); /* tell the VGA that palette data
                                         is coming. */
    for (i = 0; i < 256 * 3; i++)
        outp(PALETTE_DATA, palette[i]); /* write the data */
}
void BlitSprite(byte far* spr, int x, int y)
{
    byte far* p;
    int height, width;
    p = offScreen + (y << 8) + (y << 6) + x;

    memcpy(&width, spr, 2);
    spr += 2;
    memcpy(&height, spr, 2);
    spr += 2;

    while (height--)
    {
        memcpy(p, spr, width);
        spr += width;
        p += SCREEN_WIDTH;
    }
}
void TextInit(void)
{
    struct REGPACK reg;

    reg.r_ax = 0x1130; /*current set*/
    reg.r_bx = 0x0300; /*8x8 info*/

    intr(0x10, &reg);

    F8x8Ptr = MK_FP(reg.r_es, reg.r_bp);

    reg.r_ax = 0x1130;
    reg.r_bx = 0x0200; /*8x14 info*/

    intr(0x10, &reg);

    F8x14Ptr = MK_FP(reg.r_es, reg.r_bp);

    FontPtr = F8x8Ptr;
    charWidth = 8;
    charHeight = 8;
}
void SetFont(int FontID)
{
    if (FontID == 0)
    {
        FontPtr = F8x8Ptr;
        charHeight = 8;
        charWidth = 8;
    }
    else if (FontID == 1)
    {
        FontPtr = F8x14Ptr;
        charWidth = 8;
        charHeight = 14;
    }
    else
    {
        /* to do */
    }
}
void DrawChar(int c, int x, int y, int fgColor)
{
    byte far* p, far* fnt;
    int width, height, adj;
    byte mask;

    p = offScreen + (y << 8) + (y << 6) + x;
    adj = SCREEN_WIDTH - charWidth;

    fnt = FontPtr + c * charHeight;

    height = charHeight;
    while (height--)
    {
        width = charWidth;
        mask = 128;
        while (width--)
        {
            if ((*fnt) & mask)
            {
                *p++ = fgColor;
            }
            else
            {
                p++;
            }
            mask >>= 1;
        }
        p += adj;
        fnt++;
    }
}
void DrawString(char* str, int x, int y, int fgColor)
{
    int i = 0;
    while (str[i] != '\0')
    {
        DrawChar(str[i], x + charWidth * i, y, fgColor);
        i++;
    }
}
dword GetTick(void)
{
    return (TICKS);
}
void GetSprite(byte far* spr, int x, int y, int width, int height)
{
    byte far* p;

    p = offScreen + (y << 8) + (y << 6) + x;

    memcpy(spr, &width, 2);
    spr += 2;
    memcpy(spr, &height, 2);
    spr += 2;

    while (height--)
    {
        memcpy(spr, p, width);
        spr += width;
        p += SCREEN_WIDTH;
    }
}
void GetSpriteFromSprite(byte far* destSprite, byte far* sourceSprite, int x, int y, int width, int height)
{
    int sWidth;
    byte far* p;

    memcpy(&sWidth, sourceSprite, 2);
    sourceSprite += 4;

    p = sourceSprite + y * sWidth + x;

    memcpy(destSprite, &width, 2);
    destSprite += 2;
    memcpy(destSprite, &height, 2);
    destSprite += 2;

    while (height--)
    {
        memcpy(destSprite, p, width);
        destSprite += width;
        p += sWidth;
    }
}
void LoadToSprite(BITMAP* bmp, SPRITE* spr)
{
    spr->x = 0;
    spr->y = 0;
    spr->dx = 0;
    spr->dy = 0;
    spr->reverse = 0;
    spr->visible = 0;
    spr->untouchable = 0;
    spr->lives = 3;
    spr->width = bmp->width;
    spr->height = bmp->height;
    spr->maxX = SCREEN_WIDTH - bmp->width - 1;
    spr->maxY = SCREEN_HEIGHT - bmp->height - 1;
    spr->erase = malloc(bmp->width * bmp->height + 4);
    spr->data = bmp->data;
    /*memcpy(spr->data, bmp->data, sizeof(bmp->data));
    /*GetSprite(spr->erase, spr->x, spr->y, spr->width, spr->height);*/
    spr->oldX = spr->x;
    spr->oldY = spr->y;
}
void DrawTransparentSprite(SPRITE* spr)
{
    int i, j;
    word screenOffset = (spr->y << 8) + (spr->y << 6);
    word spriteOffset = 0;
    byte data;

    for (j = 0; j < spr->height; j++)
    {
        for (i = 0; i < spr->width; i++, spriteOffset++)
        {
            data = spr->data[spriteOffset];
            if (data)
                if (spr->reverse)
                    offScreen[screenOffset + spr->x + spr->width - 1 - i] = data;
                else
                    offScreen[screenOffset + spr->x + i] = data;
        }
        screenOffset += SCREEN_WIDTH;
    }
}

void AnimateBmp(void)
{
    int done = 0, pause = 0;
    unsigned long nextTime, nextSoundTime, nextUntouchableTime;
    unsigned int size;
    int key = 999;
    int loop;
    int i;
    int points = 0;
    int soundState = 0;
    char pointString[3] = "00";
    byte far* eraseGameOver;
    byte far* erasePoints;
    byte far* eraseLives;
    BITMAP bmp;
    SPRITE neptunia, alien, bullet, enemyBullet, neptuniaShoted;
    byte far* tmp;

    LoadBmp("nep2.bmp", &bmp);
    LoadToSprite(&bmp, &neptunia);
    tmp = neptunia.data;

    LoadBmp("nep2_sh.bmp", &bmp);
    LoadToSprite(&bmp, &neptuniaShoted);

    LoadBmp("alien.bmp", &bmp);
    LoadToSprite(&bmp, &alien);

    LoadBmp("shot.bmp", &bmp);
    LoadToSprite(&bmp, &bullet);

    LoadBmp("enemyb.bmp", &bmp);
    LoadToSprite(&bmp, &enemyBullet);
    free(bmp.data);
    free(bmp.palette);

    eraseGameOver = malloc(12 * charWidth * charHeight + 4);
    erasePoints = malloc(8 * charWidth * charHeight + 4);
    eraseLives = malloc(8 * charWidth * charHeight + 4);

    neptunia.x = 150;
    neptunia.y = 100;
    neptunia.oldX = neptunia.x;
    neptunia.oldY = neptunia.y;
    neptunia.visible = 1;

    alien.x = 270;
    alien.y = 100;
    alien.oldX = alien.x;
    alien.oldY = alien.y;
    alien.visible = 1;

    alien.dx = -2;
    alien.dy = -2;

    UpdateBuffer();
    GetSprite(neptunia.erase, neptunia.x, neptunia.y, neptunia.width, neptunia.height);
    GetSprite(alien.erase, alien.x, alien.y, alien.width, alien.height);
    GetSprite(erasePoints, 314 - 8 * charWidth, 185, 8 * charWidth, charHeight);
    GetSprite(eraseLives, 314 - 8 * charWidth, 0, 7 * charWidth, charHeight);

    InitTimer();
    nextTime = fastTick + 3;

    srand(time(NULL));
    DrawRandomBg();

    DrawString("ALIEN", 5, 185, 5);
    DrawString("SHOOTER", 5 + 6 * charWidth, 185, 07);

    while (!done)
    {
        if (!pause)
        {
            BlitSprite(neptunia.erase, neptunia.oldX, neptunia.oldY);
            BlitSprite(erasePoints, 314 - 8 * charWidth, 185);
            BlitSprite(eraseLives, 314 - 8 * charWidth, 0);
            if (alien.visible)
                BlitSprite(alien.erase, alien.oldX, alien.oldY);
            if (bullet.visible)
            {
                if (bullet.x < 0 || bullet.x > bullet.maxX)
                {
                    bullet.visible = 0;
                }
                else
                {
                    if (alien.visible && ChceckCollision(&alien, &bullet, alien.oldX, alien.oldY, bullet.oldX, bullet.oldY))
                    {
                        bullet.visible = 0;
                        points++;
                        sprintf(pointString, "%02d", points);
                        /*alien.visible = 0;*/
                        BlitSprite(alien.erase, alien.oldX, alien.oldY);
                        alien.x = rand() % SCREEN_WIDTH;
                        while (abs(alien.x - neptunia.x) < 100)
                        {
                            alien.x = rand() % SCREEN_WIDTH;
                        }

                        alien.y = rand() % SCREEN_HEIGHT;
                        while (abs(alien.y - neptunia.y) < 50)
                        {
                            alien.y = rand() % SCREEN_HEIGHT;
                        }

                        alien.oldX = alien.x;
                        alien.oldY = alien.y;
                        GetSprite(alien.erase, alien.x, alien.y, alien.width, alien.height);
                        if (alien.dx < 0)
                            alien.dx--;
                        else
                            alien.dx++;
                        if (alien.dy < 0)
                            alien.dy--;
                        else
                            alien.dy++;
                    }
                }
                BlitSprite(bullet.erase, bullet.oldX, bullet.oldY);
            }

            if (enemyBullet.visible)
            {
                if (enemyBullet.x < 0 || enemyBullet.x > enemyBullet.maxX)
                {
                    enemyBullet.visible = 0;
                }
                else
                {
                    if (neptunia.untouchable == 0 && ChceckCollision(&neptunia, &enemyBullet, neptunia.oldX, neptunia.oldY, enemyBullet.oldX, enemyBullet.oldY))
                    {
                        neptunia.lives--;
                        neptunia.data = neptuniaShoted.data;
                        neptunia.untouchable = 1;
                        nextUntouchableTime = fastTick;
                        if (neptunia.lives == 0)
                        {
                            neptunia.lives = 3;
                            neptunia.untouchable = 0;
                            neptunia.data = tmp;
                            GameOver(eraseGameOver, points);

                            enemyBullet.visible = 0;
                            neptunia.x = 150;
                            neptunia.y = 100;
                            alien.x = 270;
                            alien.y = 100;
                            alien.oldX = alien.x;
                            alien.oldY = alien.y;
                            alien.visible = 1;
                            alien.reverse = 0;
                            alien.dx = -2;
                            alien.dy = -2;
                            points = 0;
                            sprintf(pointString, "%02d", points);
                        }
                    }
                }
                BlitSprite(enemyBullet.erase, enemyBullet.oldX, enemyBullet.oldY);
            }
            if (alien.visible)
                if (neptunia.untouchable == 0 && ChceckCollision(&neptunia, &alien, neptunia.oldX, neptunia.oldY, alien.oldX, alien.oldY))
                {
                    neptunia.lives--;
                    neptunia.data = neptuniaShoted.data;
                    neptunia.untouchable = 1;
                    nextUntouchableTime = fastTick;
                    if (neptunia.lives == 0)
                    {
                        neptunia.lives = 3;
                        neptunia.untouchable = 0;
                        neptunia.data = tmp;
                        GameOver(eraseGameOver, points);

                        neptunia.x = 150;
                        neptunia.y = 100;
                        alien.x = 270;
                        alien.y = 100;
                        alien.oldX = alien.x;
                        alien.oldY = alien.y;
                        alien.visible = 1;
                        alien.reverse = 0;
                        alien.dx = -2;
                        alien.dy = -2;
                        points = 0;
                        sprintf(pointString, "%02d", points);
                    }
                }

            if (fastTick > nextTime)
            {
                loop = fastTick - nextTime;
                while (loop--)
                {
                    if (alien.visible)
                    {
                        alien.x += alien.dx;
                        alien.y += alien.dy;
                    }
                    if (neptunia.x + neptunia.dx > 0 && neptunia.x + neptunia.dx < neptunia.maxX)
                        neptunia.x += neptunia.dx;
                    if (neptunia.y + neptunia.dy > 0 && neptunia.y + neptunia.dy < neptunia.maxY)
                        neptunia.y += neptunia.dy;
                    if (bullet.visible)
                        bullet.x += bullet.dx;
                    if (enemyBullet.visible)
                        enemyBullet.x += enemyBullet.dx;
                }
                if (alien.visible)
                {
                    if (alien.x < 0)
                    {
                        alien.x = 0;
                        alien.dx = -alien.dx;
                    }
                    else if (alien.x > alien.maxX)
                    {
                        alien.x = alien.maxX;
                        alien.dx = -alien.dx;
                    }

                    if (alien.y < 0)
                    {
                        alien.y = 0;
                        alien.dy = -alien.dy;
                    }
                    else if (alien.y > alien.maxY)
                    {
                        alien.y = alien.maxY;
                        alien.dy = -alien.dy;
                    }

                    if (alien.dx < 0)
                        alien.reverse = 0;
                    else
                        alien.reverse = 1;

                    if (bullet.reverse == 1)
                        bullet.dx = -18;
                    else
                        bullet.dx = 18;
                    if (!enemyBullet.visible && rand() % 30 == 7)
                    {
                        if (alien.reverse == 0)
                            enemyBullet.dx = -6;
                        else
                            enemyBullet.dx = 6;
                        enemyBullet.visible = 1;
                        enemyBullet.reverse = alien.reverse;
                        enemyBullet.x = alien.x + alien.width / 2 - alien.width / 2;
                        enemyBullet.y = alien.y + alien.height / 2;

                        enemyBullet.oldX = enemyBullet.x;
                        enemyBullet.oldY = enemyBullet.y;
                    }
                    if (soundState && nextSoundTime + 3 < fastTick)
                        sound(300);

                    if (soundState && nextSoundTime + 6 < fastTick)
                    {
                        soundState = 0;

                        nosound();
                    }
                }

                nextTime = fastTick + 3;

                GetSprite(neptunia.erase, neptunia.x, neptunia.y, neptunia.width, neptunia.height);
                neptunia.oldX = neptunia.x;
                neptunia.oldY = neptunia.y;
                if (alien.visible)
                {
                    GetSprite(alien.erase, alien.x, alien.y, alien.width, alien.height);
                    alien.oldX = alien.x;
                    alien.oldY = alien.y;
                }
                if (bullet.visible)
                {
                    GetSprite(bullet.erase, bullet.x, bullet.y, bullet.width, bullet.height);
                    bullet.oldX = bullet.x;
                    bullet.oldY = bullet.y;
                }
                GetSprite(erasePoints, 314 - 8 * charWidth, 185, 8 * charWidth, charHeight);
                GetSprite(eraseLives, 314 - 8 * charWidth, 0, 7 * charWidth, charHeight);
                if (enemyBullet.visible)
                {
                    GetSprite(enemyBullet.erase, enemyBullet.x, enemyBullet.y, enemyBullet.width, enemyBullet.height);
                    enemyBullet.oldX = enemyBullet.x;
                    enemyBullet.oldY = enemyBullet.y;
                }

                DrawTransparentSprite(&neptunia);

                if (alien.visible)
                    DrawTransparentSprite(&alien);
                if (bullet.visible)
                    DrawTransparentSprite(&bullet);
                if (enemyBullet.visible)
                    DrawTransparentSprite(&enemyBullet);

                DrawString("LIVES", 314 - 8 * charWidth, 0, 07);
                DrawChar(neptunia.lives + '0', 314 - (charWidth << 1), 0, 07);
                DrawString("SCORE", 314 - 8 * charWidth, 185, 07);
                DrawString(pointString, 314 - (charWidth << 1), 185, 07);

                UpdateBuffer();
            }
            /*if (kbhit())
            if (getchar() == ESC)
                done = 1;*/
        }

        if (nextUntouchableTime + 112 < fastTick)
        {
            neptunia.untouchable = 0;
            neptunia.data = tmp;
        }
        while (scanKeyCodeQuequeHead != scanKeyCodeQuequeTail)
        {
            key = *(scanKeyCodeQueque + scanKeyCodeQuequeHead);
            ++scanKeyCodeQuequeHead;

            if (!(key & KEY_UP_MASK))
            {
                key &= 127;
                switch (key)
                {
                case 72:
                    neptunia.dy = -6;
                    break;
                case 80:
                    neptunia.dy = 6;
                    break;
                case 77:
                    neptunia.dx = 6;
                    neptunia.reverse = 0;
                    break;
                case 75:
                    neptunia.dx = -6;
                    neptunia.reverse = 1;
                    break;
                case 1:
                    done = 1;
                    break;
                case 57:
                    if (pause == 0)
                    {
                        pause = 1;
                        GetSprite(eraseGameOver, 150, 92, 5 * charWidth, charHeight);
                        DrawString("PAUSE", 150, 92, 13);
                        UpdateBuffer();
                        BlitSprite(eraseGameOver, 150, 92);
                    }
                    else
                    {
                        pause = 0;
                        nextTime = fastTick + 3;
                    }

                    break;
                case 45:
                    if (!bullet.visible)
                    {
                        sound(200);
                        nextSoundTime = fastTick;
                        soundState = 1;
                        bullet.visible = 1;
                        if (neptunia.reverse == 1)
                            bullet.dx = -18;
                        else
                            bullet.dx = 18;

                        bullet.reverse = neptunia.reverse;
                        bullet.x = neptunia.x + neptunia.width / 2 - bullet.width / 2;
                        bullet.y = neptunia.y + neptunia.height / 2;

                        bullet.oldX = bullet.x;
                        bullet.oldY = bullet.y;
                        GetSpriteFromSprite(bullet.erase, neptunia.erase, neptunia.width / 2 - bullet.width / 2, neptunia.height / 2, bullet.width, bullet.height);
                    }
                }
            }
            else
            {
                key &= 127;
                switch (key)
                {
                case 72:
                    neptunia.dy = 0;
                    break;
                case 80:
                    neptunia.dy = 0;
                    break;
                case 77:
                    neptunia.dx = 0;
                    break;
                case 75:
                    neptunia.dx = 0;
                    break;
                }
            }
        }
    }

    free(eraseGameOver);
    free(erasePoints);
    free(eraseLives);
    free(neptunia.erase);
    free(neptunia.data);
    free(alien.erase);
    free(alien.data);
    free(bullet.erase);
    free(bullet.data);
    free(enemyBullet.erase);
    free(enemyBullet.data);

    DeinitTimer();
}
void GameOver(byte far* eraseGameOver, int point)
{
    int i, j, poz = -1, color = 246;
    char pointString[4];
    RECORD* tmpRanking = ranking;
    RECORD* buffor;

    nosound();
    GetSprite(eraseGameOver, 112, 92, 12 * charWidth, charHeight);
    DrawString("GAME OVER!!!", 112, 92, 246);
    UpdateBuffer();
    sleep(1);
    BlitSprite(eraseGameOver, 112, 92);

    for (i = 0; i < 10; i++)
    {
        if (tmpRanking->points < point)
        {
            poz = i;
            buffor = malloc(sizeof(RECORD) * (10 - i));
            memcpy(buffor, tmpRanking, sizeof(RECORD) * (10 - i));
            tmpRanking++;
            memcpy(tmpRanking, buffor, sizeof(RECORD) * (9 - i));
            tmpRanking--;
            strcpy(tmpRanking->name, "new-");
            tmpRanking->points = point;
            free(buffor);
            break;
        }
        tmpRanking++;
    }
    tmpRanking = ranking;
    WriteFromStrucToFile(tmpRanking);

    DrawString("RANKING", 131, 5, 246);
    for (i = 0; i < 10; i++)
    {
        if (poz == i)
            color = 5;
        sprintf(pointString, "%2d", i + 1);
        pointString[2] = '.';
        pointString[3] = '\0';
        DrawString(pointString, 115 - 4 * charWidth, i * charHeight + charHeight * 2, color);
        DrawString(tmpRanking->name, 115, i * charHeight + charHeight * 2, color);
        sprintf(pointString, "%02d", tmpRanking->points);
        DrawString(pointString, 155 + 5 * charWidth, i * charHeight + charHeight * 2, color);
        tmpRanking++;
        if (poz == i)
            color = 246;
    }

    UpdateBuffer();

    scanKeyCodeEx = scanKeyCode;
    while (DetectAnyKeyPress() != 0)
        ;

    memset(offScreen, 0, SCREEN_SIZE);

    DrawRandomBg();
    DrawString("ALIEN", 5, 185, 5);
    DrawString("SHOOTER", 5 + 6 * charWidth, 185, 07);
    UpdateBuffer();
}
void InitKeyboard(void)
{
    byte far* keyState;

    oldkb = getvect(9);
    keyState = MK_FP(0x040, 0x017);
    *keyState &= (~(32 | 64));

    oldkb();

    scanKeyCodeQuequeHead = 0;
    scanKeyCodeQuequeTail = 0;
    scanKeyCode = 0;

    setvect(9, GetScan);
}
int DetectAnyKeyPress()
{
    if (scanKeyCodeEx == scanKeyCode)
        return -1;

    scanKeyCodeQuequeHead = 0;
    scanKeyCodeQuequeTail = 0;
    scanKeyCode = 0;
    return 0;
}
void DeinitKeyboard(void)
{
    setvect(9, oldkb);
}
void interrupt GetScan(void)
{
    asm cli;
    asm in al, 060h; /* read scan code */
    asm mov scanKeyCode, al;
    asm in al, 061h; /* read keyboard status */
    asm mov bl, al;
    asm or al, 080h;
    asm out 061h, al; /* set bit 7 and write */
    asm mov al, bl;
    asm out 061h, al; /* write agian, bit 7 clear */

    asm mov al, 020h; /* reset PIC */
    asm out 020h, al;

    /* end of re-set code */

    asm sti;

    *(scanKeyCodeQueque + scanKeyCodeQuequeTail) = scanKeyCode;
    ++scanKeyCodeQuequeTail;
}
int ChceckCollision(SPRITE* spr1, SPRITE* spr2, int x1, int y1, int x2, int y2)
{
    if (((x1 + 8) < (x2 + spr2->width)) && ((x1 - 8 + spr1->width) > x2) && ((y1 + 8) < (y2 + spr2->height)) && ((y1 - 8 + spr1->height) > y2))
        return 1;
    return 0;
}
static void interrupt NewTimer(void)
{
    asm cli;
    fastTick++;

    if (!(fastTick & 3))
    {
        oldtimer();
        slowTick++;
    }
    else
    {
        asm mov al, 0x20;
        asm out 0x20, al;
    }

    asm sti;
}
void InitTimer(void)
{

    slowTick = fastTick = 0l;
    oldtimer = getvect(8);

    asm cli;

    asm mov bx, 18078; /* (1193180/60)*/
    asm mov al, 00110110b;
    asm out 43h, al;
    asm mov al, bl;
    asm out 40h, al;
    asm mov al, bh;
    asm out 40h, al;

    setvect(8, NewTimer);

    asm sti;
}
void DeinitTimer(void)
{
    asm cli;

    /* slow down clock   1193180 / 65536 = 18.2 */

    asm xor bx, bx; /* min rate 18.2 Hz when set to zero*/
    asm mov al, 00110110b;
    asm out 43h, al;
    asm mov al, bl;
    asm out 40h, al;
    asm mov al, bh;
    asm out 40h, al;

    setvect(8, oldtimer); /* restore oldtimer*/

    asm sti;
}
int ReadFromFileToStruc(RECORD* rp)
{
    int i;
    FILE* fp;

    fp = fopen("ranking.dat", "rb");
    if (!fp)
        return -1;
    for (i = 0; i < 10; i++)
    {
        fread(rp++, sizeof(RECORD), 1, fp);
    }
    fclose(fp);
    return 0;
}
int WriteFromStrucToFile(RECORD* rp)
{
    int i;
    FILE* fp;

    fp = fopen("ranking.dat", "wb");
    if (!fp)
        return -1;
    for (i = 0; i < 10; i++)
    {
        fwrite(rp++, sizeof(RECORD), 1, fp);
    }
    fclose(fp);
    return 0;
}
void DrawRandomBg(void)
{
    int i;
    for (i = 0; i < 50; i++)
        offScreen[(rand() % SCREEN_HEIGHT << 6) + (rand() % SCREEN_HEIGHT << 8) + rand() % SCREEN_WIDTH] = rand() % MAX_COLOR_NUM;
}