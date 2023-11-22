#include "math.h"

const char SI_PREFIXES[8] = {'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

/**
 * Store a string representation of `n' that is `w' characters long in string
 * `s'. Uses SI prefixes for numbers that are too big. As much of the decimal as
 * possible will be shown.
 */
void tofixed(char *s, int w, double n)
{
    int postfix = 0;
    while (n >= 1000) {
        ++postfix;
        n /= 1000;
    }

    if (n < 0) {
        s[0] = '-';
        n *= -1;
    }
    else {
        s[0] = '+';
    }

    int temp = n;
    int i = s[0] == '-';

    // Whole number part {{
    if (n > 1) {
        int num_digits = 0;
        while (temp > 0) {
            temp /= 10;
            ++num_digits;
        }
        for (int j = num_digits - 1; j >= 0; j--) {
            int digit = (int)(n / pow(10, j)) % 10;
            s[i++] = digit + '0';
            if (i + !!postfix == w) break; // oh no! no more space.
        }
    }
    else {
        s[i++] = '0';
    }
    // }}

    // decimal part {{
    if (i + !!postfix < w) s[i++] = '.';
    for (; i + !!postfix < w; ++i) {
        n *= 10;
        s[i] = (n ? ((int)n % 10) : 0) + '0';
    }
    // }}

    if (postfix) s[i] = SI_PREFIXES[postfix - 1];
}

char output[] = " | CPU: 0000% 0000° 0000 | GPU: 0000% 00° 0000 | VRAM: "
                "0000 | MEM: 0000 | /: 00000 | WKD, MNT DD hh:mm:ss\n";
#define CPU_UTIL 8
#define CPU_TEMP CPU_UTIL + 6
#define CPU_SPEED CPU_TEMP + 7
#define GPU_UTIL CPU_SPEED + 12
#define GPU_TEMP GPU_UTIL + 6
#define GPU_SPEED GPU_TEMP + 5
#define VRAM GPU_SPEED + 13
#define MEM VRAM + 12
#define DISK MEM + 10
#define WEEKDAY DISK + 8
#define MONTH WEEKDAY + 5
#define DAY MONTH + 4
#define HOUR DAY + 3
#define MINUTE HOUR + 3
#define SECOND MINUTE + 3


int main(int argc, char **argv)
{

  int gpu_temp = 100;
  int gpu_usage = 100;
  int gpu_speed = 100;
  int vram_used = 100;

  tofixed(&output[GPU_UTIL], 4, gpu_usage);
  tofixed(&output[GPU_TEMP], 2, gpu_temp);
  tofixed(&output[GPU_SPEED], 4, gpu_speed * 1000000);
  tofixed(&output[VRAM], 4, vram_used * 1000000);
}
