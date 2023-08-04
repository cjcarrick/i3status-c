#define BENCH 1

#include <fstream>
#include <functional>
#include <ifaddrs.h>
#include <iomanip>
#include <iostream>
#include <net/if.h>
#include <netdb.h>
#include <sys/statfs.h>
#include <thread>
#include <unistd.h>

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::thread;

const string MONTHS[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Nov", "Dec",
};

const string WEEKDAYS[7] = {
    "Sunday",   "Monday", "Tuesday",  "Wednesday",
    "Thursday", "Friday", "Saturday",
};

// (I find it hard to believe that anything over T will be useful)
const char SI_PREFIXES[9] = {' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

bool is_num(const char &c)
{
    return c >= '0' && c <= '9';
}

/**
 * Convert n to a string of at least length w (*)
 *
 * - If the integer part of n is too big to fit in w, then the resulting string
 *   will be bigger than w
 * - set si_prefix to true to convert n to be between 1 and 1000, and add an SI
 *   prefix. Ex: (4193885, 4) becomes 4.1M instead of 4193885
 *
 * (*) a string that is USUALLY of length n. In some cases it can be n-1.
 */
string tofixed(double n, int w, bool si_prefix = true)
{
    string s;
    bool negative = false;

    if (n < 0) {
        negative = true;
        w -= 1;
        n = -n;
    }

    int prefix = 0;
    if (si_prefix) {
        while (n >= 1000) {
            n /= 1000;
            prefix++;
        }
    }
    if (prefix > 0) {
        w -= 1;
    }

    if (n < 1 && n > -1) {
        if (w >= 3) {
            s += "0.";
            w -= 2;
        }
        else {
            s += "0";
            w -= 1;
            goto finish;
        }
    }

    else {

        double orig_n = n;

        while (n > 1) {
            s = char(int(n) % 10 + '0') + s;
            n /= 10;
            w -= 1;
        }
        if (n == 1) {
            n += '1';
            w -= 1;
        }

        // Note:
        // - With w <= 0, inputs (1234567, 3) produce "1.M"
        // - With w <= 1, inputs (1234567, 3) produce "1M"
        // The former is technically probably right but it looks weird so i'm
        // using the latter
        if (w <= 1) goto finish;

        s += '.';
        w -= 1;

        if (w == 0) goto finish;

        n = orig_n;
    }

    // Do as much of the decimal part as we can in order to fit
    for (int i = 0; i <= w; i++) {
        n *= 10;
        s = s + char(int(n) % 10 + '0');
        w -= 1;
    }

finish:
    if (negative) {
        s = '-' + s;
    }
    if (prefix) {
        s = s + SI_PREFIXES[prefix];
    }
    while (w > 0) {
        // Pad left with spaces; fix for this issue described above
        s = ' ' + s;
        w--;
    }
    return s;
}

/** num is converted to string and appended to str. Guaranteed to have length of
 * `pad` or greater. `pad` will pad the string with leading zeroes. */
template <typename T> string num_to_str(T num, int pad = 0)
{
    string s;
    while (num > 0) {
        s = char(num % 10 + '0') + s;
        pad--;
        num /= 10;
    }
    while (pad > 0) {
        s = '0' + s;
        pad--;
    }
    return s;
}

/** Determine used disk space at / */
void get_disk(double *used)
{
    struct statfs a;
    statfs("/", &a);
    *used = (a.f_blocks - a.f_bfree) * a.f_bsize;
}

/** nvidia-smi */
void get_gpu(int *vram_used, int *gpu_usage, int *gpu_temp, int *gpu_speed)
{
    FILE *subproc = popen(
        "nvidia-smi "
        "--query-gpu="
        "memory.used,"
        "utilization.memory,"
        "temperature.gpu,"
        "clocks.current.graphics "
        "--format=csv,noheader,nounits",
        "r"
    );
    char c;

    *vram_used = 0;
    while (is_num(c = fgetc(subproc))) {
        *vram_used = *vram_used * 10 + (c - '0');
    }
    fgetc(subproc); // Skip space

    *gpu_usage = 0;
    while (is_num(c = fgetc(subproc))) {
        *gpu_usage = *gpu_usage * 10 + (c - '0');
    }
    fgetc(subproc); // Skip space

    *gpu_temp = 0;
    while (is_num(c = fgetc(subproc))) {
        *gpu_temp = *gpu_temp * 10 + (c - '0');
    }
    fgetc(subproc); // Skip space

    *gpu_speed = 0;
    while (is_num(c = fgetc(subproc))) {
        *gpu_speed = *gpu_speed * 10 + (c - '0');
    }

    pclose(subproc);
}

/** getifaddrs */
void get_net(char *ip_address)
{
    ifaddrs *head;
    getifaddrs(&head);

    for (ifaddrs *node = head; node != nullptr; node = node->ifa_next) {
        // Only use IPv4
        if (node->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // Skip loopback
        if (node->ifa_flags & IFF_LOOPBACK) {
            continue;
        }

        // Ok this is probably the address
        getnameinfo(
            node->ifa_addr, sizeof(struct sockaddr_in), ip_address, NI_MAXHOST,
            NULL, 0, NI_NUMERICHOST
        );

        // Assume there are no more addresses.
        // NOTE: If you have more than one network interface (eth, wlan, etc),
        // you may want to filter through them differently.
        return;
    }

    freeifaddrs(head);
}

/** reads /proc/meminfo */
void get_mem(long int *mem_used, long int *swap_used)
{
    std::ifstream f("/proc/meminfo");

    if (f.bad()) {
        cerr << "f bad";
        exit(1);
    }
    if (f.eof()) {
        cerr << "f ended early";
        exit(1);
    }

    string l;

    // The following relies on /proc/meminfo have consistent ordering of rows
    // and consistent row labels.

    long int mem_total, mem_free;
    while (l != "MemTotal:") {
        if (f.eof()) {
            cerr << "Could not determine total mem";
            exit(1);
        }
        f >> l;
    }
    f >> mem_total;

    while (l != "MemAvailable:") {
        if (f.eof()) {
            cerr << "Could not determine available mem";
            exit(1);
        }
        f >> l;
    }
    f >> mem_free;
    *mem_used = mem_total - mem_free;

    long int swap_total, swap_free;
    while (l != "SwapTotal:") {
        if (f.eof()) {
            cerr << "Could not determine total swap";
            exit(1);
        }
        f >> l;
    }
    f >> swap_total;

    while (l != "SwapFree:") {
        if (!(f >> l)) {
            cerr << "Could not determine free swap";
            exit(1);
        }
    }
    f >> swap_free;
    f.close();
    *swap_used = swap_total - swap_free;
}

/** reads /proc/cpuinfo */
void get_cpu_speed(double *cpu_speed)
{
    string s;
    std::ifstream f("/proc/cpuinfo");
    while (s != "MHz") {
        if (f.eof()) {
            cerr << "Could not determine cpu speed";
            exit(1);
        }
        f >> s;
    }
    f >> s; // skip colon
    f >> *cpu_speed;
}

/** reads /sys/class/hwmon */
void get_cpu_temp(double *cpu_temp)
{
    std::ifstream f("/sys/class/hwmon/hwmon0/temp1_input");
    f >> *cpu_temp;
}

/** reads /proc/stat */
void get_cpu_util(
    double *cpu_util,
    double *last_time_used,
    double *last_time_idle
)
{
    string _label;
    double user, nice, system, idle;

    std::ifstream f("/proc/stat");
    f >> _label >> user >> nice >> system >> idle;
    f.close();

    if (*last_time_idle > 0) {
        *cpu_util =
            (user + nice + system - *last_time_used) / (idle - *last_time_idle);
        // cout << (user + nice + system - last_time_used) << (idle -
        // last_time_idle) << endl; cout << utilization << endl;
    }

    *last_time_used = user + nice + system;
    *last_time_idle = idle;
}

int main()
{
    double *last_time_used = new double(0), *last_time_idle = new double(0),
           *cpu_usage = new double, *cpu_speed = new double,
           *cpu_temp = new double;
    long int *mem_used = new long int;
    long int *swap_used = new long int;
    int *gpu_speed = new int, *gpu_usage = new int, *gpu_temp = new int;
    int *vram_used = new int;
    double *disk_util = new double;
    char ip_addr[15];
    time_t timer;
    std::tm *timer_struct = nullptr;

    thread disk_t(get_disk, disk_util);
    thread mem_t(get_mem, mem_used, swap_used);
    thread gpu_t(get_gpu, vram_used, gpu_usage, gpu_temp, gpu_speed);
    thread net_t(get_net, ip_addr);
    thread cpu_utl_t(get_cpu_util, cpu_usage, last_time_used, last_time_idle);
    thread cpu_speed_t(get_cpu_speed, cpu_speed);
    thread cpu_temp_t(get_cpu_temp, cpu_temp);

#ifndef BENCH
    /** Seconds since start of program */
    int t = 0;
#endif

    timer = time(nullptr);
    timer_struct = localtime(&timer);

    disk_t.join();
    mem_t.join();
    gpu_t.join();
    net_t.join();
    cpu_utl_t.join();
    cpu_speed_t.join();
    cpu_temp_t.join();

#ifndef BENCH
    while (1) {

        if (t % 4 == 0) {
            get_gpu(vram_used, gpu_usage, gpu_temp, gpu_speed);
            get_mem(mem_used, swap_used);
            get_cpu_util(cpu_usage, last_time_used, last_time_idle);
            get_cpu_speed(cpu_speed);
            get_cpu_temp(cpu_temp);
        }

        if (t % 30 == 0) {
            get_disk(disk_util);
        }

#endif

    // clang-format off
    cout
        << "NET: " << ip_addr << " | "
        << "CPU: "  << tofixed(*cpu_usage * 100,     4, false) << "% " << tofixed(*cpu_temp / 1000, 4) << "° " << tofixed(*cpu_speed * 1000000, 4) << " │ "
        << "GPU: "  << tofixed(*gpu_usage,           4, false) << "% " << *gpu_temp << "° " << tofixed(*gpu_speed * 1000000, 4) << " │ "
        << "VRAM: " << tofixed(*vram_used * 1000000, 4) << " │ "
        << "MEM: "  << tofixed(*mem_used * 1000,     4) << " │ "
        << "SWAP: " << tofixed(*swap_used,           4) << " │ "
        << "/: "    << tofixed(*disk_util,           5) << " │ "
        << WEEKDAYS[timer_struct->tm_wday] << ", " << MONTHS[timer_struct->tm_mon] << " " << timer_struct->tm_mday << " "
        << timer_struct->tm_hour % 12 << ":" << num_to_str(timer_struct->tm_min, 2) << ":" << num_to_str(timer_struct->tm_sec, 2) << endl;
        // clang-format on

#ifndef BENCH
        sleep(1);

        t++;
    }
#endif
}
