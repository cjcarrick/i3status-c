#include <assert.h>
#include <bits/pthreadtypes.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statfs.h>
#include <time.h>
#include <unistd.h>

const char MONTHS[12][3] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Nov", "Dec",
};

const char WEEKDAYS[7][3] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

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

// (I find it hard to believe that anything over T will be useful)
const char SI_PREFIXES[8] = {'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

double last_time_used = 0, last_time_idle = 0, cpu_util = 0, cpu_speed,
       cpu_temp;
long int mem_used;
int gpu_speed, gpu_usage, gpu_temp;
long int vram_used;
double disk_util;
char ip_addr[15], vpn[96];
size_t ip_addr_len = 0, vpn_len = 0;
time_t timer;
struct tm *timer_struct = NULL;

int is_num(char c)
{
    return c >= '0' && c <= '9';
}

/**
 * Store a string representation of int `n' at string `str' that is `w'
 * characters wide. If `n' is too long, truncate it. If it's too short, pad the
 * front of it with char `c'.
 *
 * Not implemented for negative numbers.
 */
void leftpad(int n, char *str, int w, char c)
{
    // special case: 0
    if (n == 0) {
        str[w - 1] = '0';
        for (int i = 0; i < w - 1; ++i) str[i] = c;
        return;
    }

    int save_n = n;
    int num_digits = 0;
    while (n > 0) {
        n /= 10;
        ++num_digits;
    }
    n = save_n;

    // padding
    for (int i = 0; i < w - num_digits; ++i) str[i] = c;

    // the number
    for (int i = w - 1; n > 0; n /= 10) str[i--] = (n % 10) + '0';
}

/**
 * A version of leftpad designed for strings of length 2 only.
 *
 * Only works on positive numbers.
 */
void leftpad2(int n, char *str, char c)
{
    if (n < 0) {
        str[0] = c;
        str[1] = n + '0';
        return;
    }

    str[1] = n % 10 + '0';
    n /= 10;
    str[0] = n % 10 + '0';
}

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

void get_vpn(char *str, size_t *len)
{
    FILE *status = popen(
        "/usr/bin/mullvad "
        "status",
        "r"
    );
    // Mullvad's location or "None"
    if (fgetc(status) == 'D') {
        strcpy(str, "None");
        *len = 4;
    }
    else {
        int spaces = 0, c;
        while (spaces < 2) {
            c = fgetc(status);
            if (c == ' ') ++spaces;
        }
        ungetc(c, status);
        fscanf(status, "%s", str);
        *len = strlen(str);
    }
}

/** Determine used disk space at / */
struct statfs a;
void *get_disk(void *_)
{
#ifdef PROFILE
    clock_t start = clock();
#endif
    statfs("/", &a);
    disk_util = (a.f_blocks - a.f_bfree) * a.f_bsize;
    tofixed(&output[DISK], 5, disk_util);
#ifdef PROFILE
    printf("Disk: %ld\n", clock() - start);
#endif
    return NULL;
}

/** nvidia-smi */
FILE *gpu_subproc;
void *get_gpu(void *_)
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    gpu_subproc = popen(
        "/usr/bin/nvidia-smi "
        "--query-gpu="
        "memory.used,"
        "utilization.memory,"
        "temperature.gpu,"
        "clocks.current.graphics "
        "--format=csv,noheader,nounits",
        "r"
    );
    char c;

    vram_used = 0;
    while (is_num(c = fgetc(gpu_subproc))) {
        vram_used = vram_used * 10 + (c - '0');
    }
    fgetc(gpu_subproc); // Skip space

    gpu_usage = 0;
    while (is_num(c = fgetc(gpu_subproc))) {
        gpu_usage = gpu_usage * 10 + (c - '0');
    }
    fgetc(gpu_subproc); // Skip space

    gpu_temp = 0;
    while (is_num(c = fgetc(gpu_subproc))) {
        gpu_temp = gpu_temp * 10 + (c - '0');
    }
    fgetc(gpu_subproc); // Skip space

    gpu_speed = 0;
    while (is_num(c = fgetc(gpu_subproc))) {
        gpu_speed = gpu_speed * 10 + (c - '0');
    }

    pclose(gpu_subproc);

    tofixed(&output[GPU_UTIL], 4, gpu_usage);
    tofixed(&output[GPU_TEMP], 2, gpu_temp);
    tofixed(&output[GPU_SPEED], 4, gpu_speed * 1000000);
    tofixed(&output[VRAM], 4, vram_used * 1000000);

#ifdef PROFILE
    printf("GPU: %ld\n", clock() - start);
#endif

    return NULL;
}

/** getifaddrs */
void *get_ip(char *str, size_t *len)
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    struct ifaddrs *head;
    getifaddrs(&head);

    for (struct ifaddrs *node = head; node != NULL; node = node->ifa_next) {
        // Only use IPv4
        if (!node->ifa_addr || node->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // Skip loopback
        if (node->ifa_flags & IFF_LOOPBACK) {
            continue;
        }

        // Ok this is probably the address
        getnameinfo(
            node->ifa_addr, sizeof(struct sockaddr_in), str, NI_MAXHOST, NULL,
            0, NI_NUMERICHOST
        );
        *len = strlen(str);

        // Assume there are no more addresses.
        // NOTE: If you have more than one network interface (eth, wlan, etc),
        // you may want to filter through them differently.
        break;
    }
    freeifaddrs(head);

#ifdef PROFILE
    printf("IP: %ld\n", clock() - start);
#endif

    return NULL;
}

/** reads /proc/meminfo */
void *get_mem(void *_)
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    FILE *f = fopen("/proc/meminfo", "r");

    if (!f) {
        perror("f bad");
        exit(1);
    }

    char c[14];
    long int mem_total, mem_free;
    while (!strcmp(c, "MemTotal:")) {
        if (fscanf(f, "%9s", c) == EOF) {
            perror("Could not determine total mem");
            exit(1);
        }
    }
    fscanf(f, "%ld", &mem_total);
    while (!strcmp(c, "MemAvailable:")) {
        if (fscanf(f, "%13s", c) == EOF) {
            perror("Could not determine available mem");
            exit(1);
        }
    }
    fscanf(f, "%ld", &mem_free);
    mem_used = mem_total - mem_free;

    fclose(f);

    tofixed(&output[MEM], 4, mem_used * 1000);

#ifdef PROFILE
    printf("Mem: %ld\n", clock() - start);
#endif

    return NULL;
}

void *get_date(void *_)
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    timer = time(NULL);
    timer_struct = localtime(&timer);
    strncpy(&output[MONTH], MONTHS[timer_struct->tm_mon], 3);
    strncpy(&output[WEEKDAY], WEEKDAYS[timer_struct->tm_wday], 3);
    leftpad2(timer_struct->tm_mday, &output[DAY], ' ');
    leftpad2(
        timer_struct->tm_hour - (timer_struct->tm_hour > 12 ? 12 : 0),
        &output[HOUR], ' '
    );
    leftpad2(timer_struct->tm_min, &output[MINUTE], ' ');
    leftpad2(timer_struct->tm_sec, &output[SECOND], ' ');

#ifdef PROFILE
    printf("Date: %ld\n", clock() - start);
#endif
    return NULL;
}

/** reads /sys/devices/system/cpu */
void *get_cpu_speed(void *_)
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    FILE *f =
        fopen("/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", "r");
    fscanf(f, "%lf", &cpu_speed);
    fclose(f);
    tofixed(&output[CPU_SPEED], 4, cpu_speed * 1000);

#ifdef PROFILE
    printf("CPU speed: %ld\n", clock() - start);
#endif

    return NULL;
}

/** reads /sys/class/hwmon */
void *get_cpu_temp()
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    FILE *f = fopen("/sys/class/hwmon/hwmon0/temp1_input", "r");
    fscanf(f, "%lf", &cpu_temp);
    fclose(f);
    tofixed(&output[CPU_TEMP], 4, cpu_temp / 1000);

#ifdef PROFILE
    printf("CPU temp: %ld\n", clock() - start);
#endif

    return NULL;
}

/** reads /proc/stat */
void *get_cpu_util(void *_)
{
#ifdef PROFILE
    clock_t start = clock();
#endif

    double user, nice, system, idle;

    FILE *f = fopen("/proc/stat", "r");
    char *line = NULL, temp[96];
    size_t len = 96;
    getline(&line, &len, f);
    sscanf(line, "%s %lf %lf %lf %lf", temp, &user, &nice, &system, &idle);
    free(line);
    fclose(f);

    if (last_time_idle > 0) {
        cpu_util =
            (user + nice + system - last_time_used) / (idle - last_time_idle);
        // cout << (user + nice + system - last_time_used) << (idle -
        // last_time_idle) << endl; cout << utilization << endl;
    }

    last_time_used = user + nice + system;
    last_time_idle = idle;

    tofixed(&output[CPU_UTIL], 4, cpu_util * 100);

#ifdef PROFILE
    printf("CPU util: %ld\n", clock() - start);
#endif

    return NULL;
}

int main(int argc, char **argv)
{
    int one_shot = 0;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-1") == 0) {
            one_shot = 1;
        }
    }

    pthread_t threads[10];
    int thread_i;

    for (int t = 0; 1; ++t) {

#ifdef PROFILE
        clock_t start = clock();
#endif
        thread_i = 0;
        pthread_create(&threads[thread_i++], NULL, get_date, NULL);

        if (t % 4 == 0) {
            pthread_create(&threads[thread_i++], NULL, get_gpu, NULL);
            pthread_create(&threads[thread_i++], NULL, get_mem, NULL);
            pthread_create(&threads[thread_i++], NULL, get_cpu_util, NULL);
            pthread_create(&threads[thread_i++], NULL, get_cpu_temp, NULL);
            pthread_create(&threads[thread_i++], NULL, get_cpu_speed, NULL);
        }

        if (t % 4 == 0) {
            pthread_create(&threads[thread_i++], NULL, get_disk, NULL);
            get_ip(ip_addr, &ip_addr_len);
            get_vpn(vpn, &vpn_len);
        }

        // wait for any threads to finish
        for (int i = 0; i < thread_i; ++i) pthread_join(threads[i], NULL);

#ifdef PROFILE
        printf("**TOTAL: %ld\n\n", clock() - start);
#else
        fwrite("VPN: ", 1, 5, stdout);
        fwrite(vpn, 1, vpn_len, stdout);
        fwrite(" | NET: ", 1, 5, stdout);
        fwrite(ip_addr, 1, ip_addr_len, stdout);
        fwrite(output, 1, sizeof(output), stdout);
#endif

        if (one_shot) break;

        sleep(1);
    }
}
