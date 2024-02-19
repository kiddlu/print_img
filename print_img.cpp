#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize.h"

#define MULTI_THREAD_TRANSFORM

#define TERM_PADDING_X 8
#define TERM_PADDING_Y 4

static inline void get_term_size(int *width, int *height)
{
    struct winsize w;
    int            ret;
    ret = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    if (ret != 0 || (w.ws_col | w.ws_row) == 0)
    {
        *height = 24;
        *width  = 80;
        return;
    }

    *height = (int)w.ws_row;
    *width  = (int)w.ws_col;

    if (*height > 120)
    {
        *height = 120;
    }
    if (*width > 400)
    {
        *width = 400;
    }
}

static inline void get_ideal_image_size(int      *width,
                                        int      *height,
                                        const int image_width,
                                        const int image_height,
                                        int       squashing_enabled)
{
    *width              = squashing_enabled
                              ? image_width * 2
                              : image_width;  // <- NOTE: This is to offset narrow chars.
    *height             = image_height;
    double aspect_ratio = (double)*width / (double)*height;

    int term_w, term_h;
    get_term_size(&term_w, &term_h);

    term_h -= TERM_PADDING_Y;  // Some offsets for screen padding.
    term_w -= TERM_PADDING_X;

    bool solving = true;

    while (solving)
    {
        solving = false;

        if (*width > term_w)
        {
            *width -= TERM_PADDING_X;
            *height = (int)(((double)*width) / aspect_ratio);
            solving = true;
        }

        if (*height > term_h)
        {
            *height -= TERM_PADDING_Y;
            *width  = (int)(((double)*height) * aspect_ratio);
            solving = true;
        }
    }
}

static inline int print_raw_img_compat(unsigned char *img,
                                       unsigned int   width,
                                       unsigned int   height)
{

    typedef struct
    {
        unsigned char r;
        unsigned char g;
        unsigned char b;
    } pxdata_t;

    pxdata_t *data = (pxdata_t *)img;
    for (unsigned int d = 0; d < width * height; d++)
    {
        if (d % width == 0 && d != 0)
        {
            printf("\033[0m");
            printf("\n");
        }

        pxdata_t *c = data + d;
        printf("\033[48;2;%03u;%03u;%03um ", c->r, c->g, c->b);
    }
    printf("\033[0m");
    printf("\n");
    return 0;
}

const unsigned int BITMAPS[] = {
    0x00000000, 0x00a0,

    // Block graphics
    // 0xffff0000, 0x2580,  // upper 1/2; redundant with inverse lower 1/2

    0x0000000f, 0x2581,                      // lower 1/8
    0x000000ff, 0x2582,                      // lower 1/4
    0x00000fff, 0x2583, 0x0000ffff, 0x2584,  // lower 1/2
    0x000fffff, 0x2585, 0x00ffffff, 0x2586,  // lower 3/4
    0x0fffffff, 0x2587,
    // 0xffffffff, 0x2588,  // full; redundant with inverse space

    0xeeeeeeee, 0x258a,  // left 3/4
    0xcccccccc, 0x258c,  // left 1/2
    0x88888888, 0x258e,  // left 1/4

    0x0000cccc, 0x2596,  // quadrant lower left
    0x00003333, 0x2597,  // quadrant lower right
    0xcccc0000, 0x2598,  // quadrant upper left
    // 0xccccffff, 0x2599,  // 3/4 redundant with inverse 1/4
    0xcccc3333, 0x259a,  // diagonal 1/2
                         // 0xffffcccc, 0x259b,  // 3/4 redundant
    // 0xffff3333, 0x259c,  // 3/4 redundant
    0x33330000, 0x259d,  // quadrant upper right
                         // 0x3333cccc, 0x259e,  // 3/4 redundant
    // 0x3333ffff, 0x259f,  // 3/4 redundant

    // Line drawing subset: no double lines, no complex light lines

    0x000ff000, 0x2501,  // Heavy horizontal
    0x66666666, 0x2503,  // Heavy vertical

    0x00077666, 0x250f,  // Heavy down and right
    0x000ee666, 0x2513,  // Heavy down and left
    0x66677000, 0x2517,  // Heavy up and right
    0x666ee000, 0x251b,  // Heavy up and left

    0x66677666, 0x2523,  // Heavy vertical and right
    0x666ee666, 0x252b,  // Heavy vertical and left
    0x000ff666, 0x2533,  // Heavy down and horizontal
    0x666ff000, 0x253b,  // Heavy up and horizontal
    0x666ff666, 0x254b,  // Heavy cross

    0x000cc000, 0x2578,  // Bold horizontal left
    0x00066000, 0x2579,  // Bold horizontal up
    0x00033000, 0x257a,  // Bold horizontal right
    0x00066000, 0x257b,  // Bold horizontal down

    0x06600660, 0x254f,  // Heavy double dash vertical

    0x000f0000, 0x2500,  // Light horizontal
    0x0000f000, 0x2500,  //
    0x44444444, 0x2502,  // Light vertical
    0x22222222, 0x2502,

    0x000e0000, 0x2574,  // light left
    0x0000e000, 0x2574,  // light left
    0x44440000, 0x2575,  // light up
    0x22220000, 0x2575,  // light up
    0x00030000, 0x2576,  // light right
    0x00003000, 0x2576,  // light right
    0x00004444, 0x2577,  // light down
    0x00002222, 0x2577,  // light down

    // Misc technical

    0x44444444, 0x23a2,  // [ extension
    0x22222222, 0x23a5,  // ] extension

    0x0f000000, 0x23ba,  // Horizontal scanline 1
    0x00f00000, 0x23bb,  // Horizontal scanline 3
    0x00000f00, 0x23bc,  // Horizontal scanline 7
    0x000000f0, 0x23bd,  // Horizontal scanline 9

    // Geometrical shapes. Tricky because some of them are too wide.

    // 0x00ffff00, 0x25fe,  // Black medium small square
    0x00066000, 0x25aa,  // Black small square

    // 0x11224488, 0x2571,  // diagonals
    // 0x88442211, 0x2572,
    // 0x99666699, 0x2573,
    // 0x000137f0, 0x25e2,  // Triangles
    // 0x0008cef0, 0x25e3,
    // 0x000fec80, 0x25e4,
    // 0x000f7310, 0x25e5,

    0, 0,  // End marker for "regular" characters

    // Teletext / legacy graphics 3x2 block character codes.
    // Using a 3-2-3 pattern consistently, perhaps we should create automatic
    // variations....

    0xccc00000, 0xfb00, 0x33300000, 0xfb01, 0xfff00000, 0xfb02, 0x000cc000,
    0xfb03, 0xccccc000, 0xfb04, 0x333cc000, 0xfb05, 0xfffcc000, 0xfb06,
    0x00033000, 0xfb07, 0xccc33000, 0xfb08, 0x33333000, 0xfb09, 0xfff33000,
    0xfb0a, 0x000ff000, 0xfb0b, 0xcccff000, 0xfb0c, 0x333ff000, 0xfb0d,
    0xfffff000, 0xfb0e, 0x00000ccc, 0xfb0f,

    0xccc00ccc, 0xfb10, 0x33300ccc, 0xfb11, 0xfff00ccc, 0xfb12, 0x000ccccc,
    0xfb13, 0x333ccccc, 0xfb14, 0xfffccccc, 0xfb15, 0x00033ccc, 0xfb16,
    0xccc33ccc, 0xfb17, 0x33333ccc, 0xfb18, 0xfff33ccc, 0xfb19, 0x000ffccc,
    0xfb1a, 0xcccffccc, 0xfb1b, 0x333ffccc, 0xfb1c, 0xfffffccc, 0xfb1d,
    0x00000333, 0xfb1e, 0xccc00333, 0xfb1f,

    0x33300333, 0x1b20, 0xfff00333, 0x1b21, 0x000cc333, 0x1b22, 0xccccc333,
    0x1b23, 0x333cc333, 0x1b24, 0xfffcc333, 0x1b25, 0x00033333, 0x1b26,
    0xccc33333, 0x1b27, 0xfff33333, 0x1b28, 0x000ff333, 0x1b29, 0xcccff333,
    0x1b2a, 0x333ff333, 0x1b2b, 0xfffff333, 0x1b2c, 0x00000fff, 0x1b2d,
    0xccc00fff, 0x1b2e, 0x33300fff, 0x1b2f,

    0xfff00fff, 0x1b30, 0x000ccfff, 0x1b31, 0xcccccfff, 0x1b32, 0x333ccfff,
    0x1b33, 0xfffccfff, 0x1b34, 0x00033fff, 0x1b35, 0xccc33fff, 0x1b36,
    0x33333fff, 0x1b37, 0xfff33fff, 0x1b38, 0x000fffff, 0x1b39, 0xcccfffff,
    0x1b3a, 0x333fffff, 0x1b3b,

    0, 1  // End marker for extended TELETEXT mode.
};

typedef struct
{
    int fg_color[3];
    int bg_color[3];
    int codepoint;
} chardata_t;

#define cstd_max(a, b)          \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })

#define cstd_min(a, b)          \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })

static inline int cstd_bitcount(unsigned int n)
{
    int count = 0;
    while (n)
    {
        if (n & 1)
            count++;
        n = n >> 1;
    }
    return count;
}

// Return a chardata struct with the given code point and corresponding averag
// fg and bg colors.
static inline chardata_t create_chardata(unsigned char *image,
                                         int            x0,
                                         int            y0,
                                         int            width,
                                         int            heigh,
                                         int            codepoint,
                                         int            pattern)
{
    chardata_t result;
    memset(&result, 0, sizeof(chardata_t));
    result.codepoint         = codepoint;
    int          fg_count    = 0;
    int          bg_count    = 0;
    unsigned int mask        = 0x80000000;
    int          pixel_index = 0;

    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            int *avg;
            if (pattern & mask)
            {
                avg = &(result.fg_color[0]);
                fg_count++;
            }
            else
            {
                avg = &(result.bg_color[0]);
                bg_count++;
            }
            pixel_index = ((x0 + x) + width * (y0 + y)) * 3;
            for (int i = 0; i < 3; i++)
            {
                avg[i] += *(image + pixel_index + i);
            }
            mask = mask >> 1;
        }
    }

    // Calculate the average color value for each bucket
    for (int i = 0; i < 3; i++)
    {
        if (bg_count != 0)
        {
            result.bg_color[i] /= bg_count;
        }
        if (fg_count != 0)
        {
            result.fg_color[i] /= fg_count;
        }
    }
    return result;
}

// Find the best character and colors for a 4x8 part of the image at the given
// position
static inline chardata_t find_chardata(unsigned char *image,
                                       int            x0,
                                       int            y0,
                                       int            width,
                                       int            height)
{
    int min[3]      = {255, 255, 255};
    int max[3]      = {0};
    int pixel_index = 0;

// c++ map
#if 0
#include <map>
  std::map<long,int> count_per_color;

  // Determine the minimum and maximum value for each color channel
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 4; x++) {
      long color = 0;
      pixel_index = ((x0 + x) + width*(y0 + y)) * 3;
      for (int i = 0; i < 3; i++) {
        int d = *(image + pixel_index + i);
        min[i] = cstd_min(min[i], d);
        max[i] = cstd_max(max[i], d);
        color = (color << 8) | d;
      }
      count_per_color[color]++;
    }
  }

  std::multimap<int,long> color_per_count;
  for (auto i = count_per_color.begin(); i != count_per_color.end(); ++i) {
    color_per_count.insert(std::pair<int,long>(i->second, i->first));
  }

  auto iter = color_per_count.rbegin();
  int count2 = iter->first;
  long max_count_color_1 = iter->second;
  long max_count_color_2 = max_count_color_1;
  if ((++iter) != color_per_count.rend()) {
    count2 += iter->first;
    max_count_color_2 = iter->second;
  }

  unsigned int bits = 0;
  bool direct = count2 > (8*4) / 2;
#else
    // Determine the minimum and maximum value for each color channel
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            long color  = 0;
            pixel_index = ((x0 + x) + width * (y0 + y)) * 3;
            for (int i = 0; i < 3; i++)
            {
                int d  = *(image + pixel_index + i);
                min[i] = cstd_min(min[i], d);
                max[i] = cstd_max(max[i], d);
                color  = (color << 8) | d;
            }
        }
    }

    int  count2            = 0;
    long max_count_color_1 = 0;
    long max_count_color_2 = 0;

    unsigned int bits       = 0;
    bool         direct     = false;
#endif

    if (direct)
    {
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                bits        = bits << 1;
                int d1      = 0;
                int d2      = 0;
                pixel_index = ((x0 + x) + width * (y0 + y)) * 3;
                for (int i = 0; i < 3; i++)
                {
                    int shift = 16 - 8 * i;
                    int c1    = (max_count_color_1 >> shift) & 255;
                    int c2    = (max_count_color_2 >> shift) & 255;
                    int c     = *(image + pixel_index + i);
                    d1 += (c1 - c) * (c1 - c);
                    d2 += (c2 - c) * (c2 - c);
                }
                if (d1 > d2)
                {
                    bits |= 1;
                }
            }
        }
    }
    else
    {
        // Determine the color channel with the greatest range.
        int splitIndex = 0;
        int bestSplit  = 0;
        for (int i = 0; i < 3; i++)
        {
            if (max[i] - min[i] > bestSplit)
            {
                bestSplit  = max[i] - min[i];
                splitIndex = i;
            }
        }

        // We just split at the middle of the interval instead of computing the
        // median.
        int splitValue = min[splitIndex] + bestSplit / 2;

        // Compute a bitmap using the given split and sum the color values for
        // both buckets.
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                bits        = bits << 1;
                pixel_index = ((x0 + x) + width * (y0 + y)) * 3;
                if (*(image + pixel_index + splitIndex) > splitValue)
                {
                    bits |= 1;
                }
            }
        }
    }

    // Find the best bitmap match by counting the bits that don't match,
    // including the inverted bitmaps.
    int          best_diff    = 8;
    unsigned int best_pattern = 0x0000ffff;
    int          codepoint    = 0x2584;
    bool         inverted     = false;
    unsigned int end_marker   = 0;
    for (int i = 0; BITMAPS[i + 1] != end_marker; i += 2)
    {
        // Skip all end markers
        if (BITMAPS[i + 1] < 32)
        {
            continue;
        }
        unsigned int pattern = BITMAPS[i];
        for (int j = 0; j < 2; j++)
        {
            int diff = cstd_bitcount(pattern ^ bits);
            if (diff < best_diff)
            {
                best_pattern = BITMAPS[i];  // pattern might be inverted.
                codepoint    = BITMAPS[i + 1];
                best_diff    = diff;
                inverted     = best_pattern != pattern;
            }
            pattern = ~pattern;
        }
    }

    if (direct)
    {
        chardata_t result;
        if (inverted)
        {
            long tmp          = max_count_color_1;
            max_count_color_1 = max_count_color_2;
            max_count_color_2 = tmp;
        }
        for (int i = 0; i < 3; i++)
        {
            int shift          = 16 - 8 * i;
            result.fg_color[i] = (max_count_color_2 >> shift) & 255;
            result.bg_color[i] = (max_count_color_1 >> shift) & 255;
            result.codepoint   = codepoint;
        }
        return result;
    }
    return create_chardata(image, x0, y0, width, height, codepoint,
                           best_pattern);
}

static inline int clamp_byte(int value)
{
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

static inline void print_term_color(int is_bg, int r, int g, int b)
{
    r = clamp_byte(r);
    g = clamp_byte(g);
    b = clamp_byte(b);

    printf("%s%d;%d;%dm", (is_bg ? "\x1b[48;2;" : "\x1b[38;2;"), r, g, b);

    return;
}

static inline void print_codepoint(int codepoint)
{
    if (codepoint < 128)
    {
        printf("%c", (char)codepoint);
    }
    else if (codepoint < 0x7ff)
    {
        printf("%c%c", (char)(0xc0 | (codepoint >> 6)),
               (char)(0x80 | (codepoint & 0x3f)));
    }
    else if (codepoint < 0xffff)
    {
        printf("%c%c%c", (char)(0xe0 | (codepoint >> 12)),
               (char)(0x80 | ((codepoint >> 6) & 0x3f)),
               (char)(0x80 | (codepoint & 0x3f)));
    }
    else if (codepoint < 0x10ffff)
    {
        printf("%c%c%c%c", (char)(0xf0 | (codepoint >> 18)),
               (char)(0x80 | ((codepoint >> 12) & 0x3f)),
               (char)(0x80 | ((codepoint >> 06) & 0x3f)),
               (char)(0x80 | (codepoint & 0x3f)));
    }
    else
    {
        printf("codepoint ERROR\n");
    }
}

static inline int trans_to_chardata(chardata_t    *cha,
                                    unsigned char *image,
                                    int            width,
                                    int            height)
{
    chardata_t *cdata = cha;
    for (int y = 0; y < height; y = y + 8)
    {
        for (int x = 0; x < width; x = x + 4)
        {
            *cdata = find_chardata(image, x, y, width, height);
            cdata++;
        }
    }
    return 0;
}

struct trans_thread_args
{
    chardata_t    *ansi_char;
    unsigned char *image;
    int            width;
    int            height;
};

void *trans_to_chardata_thread(void *arg)
{
    struct trans_thread_args *args = (struct trans_thread_args *)arg;
    trans_to_chardata(args->ansi_char, args->image, args->width, args->height);

    return NULL;
}

static inline int print_raw_img(unsigned char *image, int width, int height)
{
    int char_width  = (width / 4);
    int char_height = (height / 8);
    int char_length = char_width * char_height * sizeof(chardata_t);

    chardata_t *chardata_scheme = (chardata_t *)malloc(char_length);

//    printf("char_width %d, height %d, length %d\n", char_width, char_height,
//           char_length);

// trans
#ifndef MULTI_THREAD_TRANSFORM
#define THREAD_NUM (1)
    for (int thread_id = 0; thread_id < THREAD_NUM; thread_id++)
    {
        trans_to_chardata(
            &chardata_scheme[char_width * char_height / THREAD_NUM * thread_id],
            image + (width * height / THREAD_NUM * thread_id) * 3, width,
            height / THREAD_NUM);
    }
#else
    int          THREAD_NUM = 1;
    for (int i = 8; i >= 2; i--)
    {
        if (char_height % i == 0)
        {
            THREAD_NUM = i;
            break;
        }
    }

    // printf("THREAD_NUM %d\n", THREAD_NUM);

    pthread_t                thread_id[THREAD_NUM];
    struct trans_thread_args args[THREAD_NUM];
    int                      num;
    for (num = 0; num < THREAD_NUM; num++)
    {
        args[num].ansi_char =
            &chardata_scheme[char_width * char_height / THREAD_NUM * num];
        args[num].image  = image + (width * height / THREAD_NUM * num) * 3;
        args[num].width  = width;
        args[num].height = height / THREAD_NUM;

        pthread_create(&(thread_id[num]), NULL, trans_to_chardata_thread,
                       (void *)&(args[num]));
    }

    for (num = 0; num < THREAD_NUM; num++)
    {
        pthread_join(thread_id[num], NULL);
    }
#endif

// draw
#if 1
    chardata_t *curr_chardata = chardata_scheme;
    chardata_t *prev_chardata = chardata_scheme;

    for (int i = 0; i < (char_width * char_height);)
    {
        if ((i % char_width) == 0 ||
            curr_chardata->bg_color != prev_chardata->bg_color)
            print_term_color(1, curr_chardata->bg_color[0],
                             curr_chardata->bg_color[1],
                             curr_chardata->bg_color[2]);
        if ((i % char_width) == 0 ||
            curr_chardata->fg_color != prev_chardata->fg_color)
            print_term_color(0, curr_chardata->fg_color[0],
                             curr_chardata->fg_color[1],
                             curr_chardata->fg_color[2]);
        print_codepoint(curr_chardata->codepoint);
        i++;
        if ((i % char_width) == 0)
            printf("\x1b[0m\n");

        prev_chardata = curr_chardata;
        curr_chardata++;
    }
#endif

    free(chardata_scheme);
    return 0;
}

int print_img(unsigned char *img,
              int            size,
              unsigned int   opt_width,
              unsigned int   opt_height,
              int            compat)
{
    int            rwidth, rheight, rchannels;
    unsigned char *read_data =
        stbi_load_from_memory(img, size, &rwidth, &rheight, &rchannels, 3);

    if (read_data == NULL)
    {
        fprintf(stderr, "Error reading image data!\n\n");
        return -1;
    }

    unsigned int desired_width, desired_height;

    int        calc_w, calc_h;
    static int last_calc_w = 0, last_calc_h = 0;
    int        squashing_enabled = 1;

    get_ideal_image_size(&calc_w, &calc_h, rwidth, rheight, squashing_enabled);

    if (last_calc_w != calc_w || calc_h != last_calc_h)
    {
        last_calc_w = calc_w;
        last_calc_h = calc_h;

        printf("\033[H\033[J");  // clear screnn
    }

    desired_width  = opt_width == 0 ? (unsigned int)calc_w * 4 : opt_width;
    desired_height = opt_height == 0 ? (unsigned int)calc_h * 8 : opt_height;

    if (compat == 1)
    {
        desired_width /= 4;
        desired_height /= 8;
    }

    // printf("desired_width %d, desired_height %d\n", desired_width,
    // desired_height);

    // Check for and do any needed image resizing...
    unsigned char *data;
    if (desired_width != (unsigned)rwidth ||
        desired_height != (unsigned)rheight)
    {
        unsigned char *new_data = (unsigned char *)malloc(
            3 * sizeof(unsigned char) * desired_width * desired_height);
        int r = stbir_resize_uint8(read_data, rwidth, rheight, 0, new_data,
                                   desired_width, desired_height, 0, 3);

        if (r == 0)
        {
            perror("Error resizing image:");
            return -1;
        }
        stbi_image_free(read_data);
        data = new_data;
    }
    else
    {
        data = read_data;
    }

    if (compat == 1)
    {
        print_raw_img_compat(data, desired_width, desired_height);
    }
    else
    {
        print_raw_img(data, desired_width, desired_height);
    }

    stbi_image_free(data);
    return 0;
}