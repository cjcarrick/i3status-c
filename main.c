#include "/opt/cuda/targets/x86_64-linux/include/nvml.h"
#include <assert.h>
#include <bits/pthreadtypes.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char SI_PREFIXES[8] = {'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};

#define SEPARATOR " | "
#define OUTPUT \
	LABEL(vpn_label, "VPN: ") VALUE(vpn, 32) \
	SEP(5) \
	LABEL(ip_label, "IP: ") VALUE(ip, 32) \
	SEP(4) \
	LABEL(cpu_label, "CPU: ") \
	VALUE(cpu_usage, 4) LABEL(cpu_usage_unit, "% ") \
	VALUE(cpu_temp, 4) LABEL(cpu_temp_unit, "° ") \
	VALUE(cpu_speed, 4) \
	SEP(1) \
	LABEL(gpu_label, "GPU: ") \
	VALUE(gpu_usage, 4) LABEL(gpu_usage_unit, "% ") \
	VALUE(gpu_temp, 4) LABEL(gpu_temp_unit, "° ") \
	VALUE(gpu_speed, 4) \
	SEP(2) \
	LABEL(vram_label, "VRAM: ") VALUE(vram, 4) \
	SEP(10) \
	LABEL(mem_label, "MEM: ") VALUE(mem, 4)\
	SEP(3) \
	LABEL(fs_label, "/ AVAIL: ") VALUE(fs, 5) \
	SEP(11) \
	VALUE(date, 21) \
	LABEL(newline, "\n")

struct
{
#define LABEL(name, val) char name[strlen(val) + 1];
#define VALUE(name, val) char name[val];
#define SEP(n) char sep##n[sizeof(SEPARATOR)];
	OUTPUT
#undef LABEL
#undef SEP
#undef VALUE
} output;


#ifdef SEM
sem_t *sem;
#endif

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
void tofixed(char *s, int w, long double n)
{
	int postfix = 0;
	while (n >= 1024) {
		++postfix;
		n /= 1024;
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

#ifdef PROFILE
#define THREAD_START                                                           \
	static clock_t start;                                                  \
	begin:                                                                 \
	start = clock();
#else
#define THREAD_START                                                           \
	begin:
#endif
#ifdef PROFILE
#define THREAD_END(label, sleep_time)                                          \
	fprintf(stderr, "%-32s: %10lu\n", label, clock() - start);             \
	sleep((int)sleep_time);                                                \
	goto begin;
#else
#define THREAD_END(label, sleep_time)                                          \
	sleep((int)sleep_time);                                                \
	goto begin;
#endif

void *get_vpn(void *arg)
{
	struct ifaddrs *head;

	THREAD_START;

	getifaddrs(&head);
	output.vpn[0] = 0;
	for (struct ifaddrs *node = head; node != NULL; node = node->ifa_next) {
		// FIXME: For now, this number seems to epresent the flags that
		// wireguard vpns get, but is there a better way to get the VPN
		// interface? What if more than one vpn is being used? 
		if (node->ifa_flags == 65745) {
			#ifdef SEM
			sem_wait(sem);
			#endif
			strncpy(output.vpn, node->ifa_name, sizeof(output.vpn));
			break;
		}
	}
	if (output.vpn[0] == 0) {
		strcpy(output.vpn, "None");
	}
#ifdef SEM
	sem_post(sem);
#endif
	freeifaddrs(head);

	THREAD_END("VPN", 3);
}

/** Determine used disk space at / */
struct statfs a;
void *get_disk(void *arg)
{
	static double disk_util;

	THREAD_START

	statfs("/", &a);
	disk_util = a.f_bfree * a.f_bsize;
#ifdef SEM
	sem_wait(sem);
#endif
	tofixed(output.fs, 5, disk_util);
#ifdef SEM
	sem_post(sem);
#endif

	THREAD_END("disk", arg)
}

void *get_gpu(void *sleep_time)
{
	nvmlReturn_t nvml_result;
	nvmlDevice_t nvml_device;
	nvmlMemory_t nvml_mem;
	unsigned int nvml_temp;
	nvmlUtilization_t nvml_util;
	unsigned int nvml_clocks;

	// initialize nvml library for get_gpu()
	nvml_result = nvmlInit();
	if (nvml_result != NVML_SUCCESS) {
		printf(
			"[nvml] Failed to initialize NVML: %s\n",
			nvmlErrorString(nvml_result)
		);
		exit(1);
	}

	nvml_result = nvmlDeviceGetHandleByIndex(0, &nvml_device);
	if (NVML_SUCCESS != nvml_result) {
		printf(
			"[nvml] Failed to get handle for device %u: %s\n", 0,
			nvmlErrorString(nvml_result)
		);
		goto nvml_err;
	}

	THREAD_START;

#ifdef SEM
	sem_wait(sem);
#endif
	nvml_result = nvmlDeviceGetMemoryInfo(nvml_device, &nvml_mem);
	if (nvml_result != NVML_SUCCESS) return NULL;
	tofixed(output.vram, sizeof(output.vram), nvml_mem.used);

	nvml_result = nvmlDeviceGetTemperature(nvml_device, NVML_TEMPERATURE_GPU, &nvml_temp);
	if (nvml_result != NVML_SUCCESS) return NULL;
	tofixed(output.gpu_temp, sizeof(output.gpu_temp), nvml_temp);

	nvml_result = nvmlDeviceGetUtilizationRates(nvml_device, &nvml_util);
	if (nvml_result != NVML_SUCCESS) return NULL;
	tofixed(output.gpu_usage, sizeof(output.gpu_usage), nvml_util.gpu);

	nvml_result = nvmlDeviceGetClockInfo(nvml_device, NVML_CLOCK_GRAPHICS, &nvml_clocks);
	if (nvml_result != NVML_SUCCESS) return NULL;
	tofixed(output.gpu_speed, sizeof(output.gpu_speed), nvml_clocks * 1000000);
#ifdef SEM
	sem_post(sem);
#endif

	THREAD_END("gpu", sleep_time)

	nvml_result = nvmlShutdown();
	if (NVML_SUCCESS != nvml_result)
		printf("Failed to shutdown NVML: %s\n", nvmlErrorString(nvml_result));
	return 0;

nvml_err:
	nvml_result = nvmlShutdown();
	if (NVML_SUCCESS != nvml_result)
		printf("Failed to shutdown NVML: %s\n", nvmlErrorString(nvml_result));
	exit(1);
}

/** getifaddrs */
void *get_ip(void *arg)
{
	THREAD_START;

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
#ifdef SEM
		sem_wait(sem);
#endif
		getnameinfo(
			node->ifa_addr, sizeof(struct sockaddr_in), output.ip, sizeof(output.ip), NULL,
			0, NI_NUMERICHOST
		);
#ifdef SEM
		sem_post(sem);
#endif

		// Assume there are no more addresses.
		// NOTE: If you have more than one network interface (eth, wlan, etc),
		// you may want to filter through them differently.
		break;
	}
	freeifaddrs(head);

	THREAD_END("ip", arg)
}

/** reads /proc/meminfo */
void *get_mem(void *arg)
{
	static FILE *f;
	static long int total, avail;
	static char *line;
	static size_t len;

	THREAD_START

	f = fopen("/proc/meminfo", "r");
	total = avail = 0;
	while (!feof(f) && (total == 0 || avail == 0)) {
		getline(&line, &len, f);
		if (!strncmp(line, "MemTotal:", sizeof("MemTotal:")-1)) {
			sscanf(line + 10, "%ld", &total);
		} else if (!strncmp(line, "MemAvailable:", sizeof("MemAvailable:")-1)) {
			sscanf(line + 14, "%ld", &avail);
		}
		free(line);
		line = NULL;
		len = 0;
	}

#ifdef SEM
	sem_wait(sem);
#endif
	tofixed(output.mem, sizeof(output.mem), (total - avail) * 1024.);
#ifdef SEM
	sem_post(sem);
#endif
	fclose(f);

	THREAD_END("Mem", arg);
}

void *get_date(void *arg)
{
	static const char MONTHS[12][4] = {
	    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};
	static const char WEEKDAYS[7][4] = {
	    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
	};
	static const char *INT_TO_STR_PAD[] = {
	    "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10",
	    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21",
	    "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32",
	    "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43",
	    "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54",
	    "55", "56", "57", "58", "59", "60",
	};
	static const char *INT_TO_STR[] = {
	    " 0", " 1", " 2", " 3", " 4", " 5", " 6", " 7", " 8", " 9", "10",
	    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21",
	    "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32",
	    "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43",
	    "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54",
	    "55", "56", "57", "58", "59", "60",
	};

	time_t t;
	struct tm tm = {0};

	memset(output.date, ' ', sizeof(output.date));
	output.date[10] = ',';
	output.date[14] = ':';
	output.date[17] = ':';
	THREAD_START;

#ifdef SEM
	sem_wait(sem);
#endif
	t = time(NULL);
	// TODO: gmtime() is much faster than localtime(). But to use gmtime(),
	// we would have to add the `timezone' global var, and also factor in
	// `daylight'.
	tm = *localtime_r(&t, &tm);
	memcpy(output.date + 0, WEEKDAYS[tm.tm_wday], 3);
	memcpy(output.date + 4, MONTHS[tm.tm_mon], 3);
	memcpy(output.date + 8, INT_TO_STR[tm.tm_mday], 2);
	memcpy(output.date + 12, INT_TO_STR[tm.tm_hour - (tm.tm_hour > 12 ? 12 : 0)], 2);
	memcpy(output.date + 15, INT_TO_STR_PAD[tm.tm_min], 2);
	memcpy(output.date + 18, INT_TO_STR_PAD[tm.tm_sec], 2);
#ifdef SEM
	sem_post(sem);
#endif

	THREAD_END("date", arg)
}

/** reads /sys/devices/system/cpu */
void *get_cpu_speed(void *arg)
{
	static char buf[32];
	static int fd, n_read;
	static int cpu_speed;
	THREAD_START;

	fd = open("/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", O_RDONLY);
	n_read = read(fd, buf, 32);
	buf[n_read] = 0;
	cpu_speed = atoi(buf);

#ifdef SEM
	sem_wait(sem);
#endif
	tofixed(output.cpu_speed, sizeof(output.cpu_speed), cpu_speed * 1000);
#ifdef SEM
	sem_post(sem);
#endif
	close(fd);

	THREAD_END("CPU Speed", arg)
}

/** reads /sys/class/hwmon */
void *get_cpu_temp(void *arg)
{
	static char buf[32];
	static int fd, n_read;
	static int cpu_temp;
	THREAD_START;

	fd = open("/sys/class/hwmon/hwmon0/temp1_input", O_RDONLY);
	n_read = read(fd, buf, 32);
	buf[n_read] = 0;
	cpu_temp = atoi(buf);

#ifdef SEM
	sem_wait(sem);
#endif
	tofixed(output.cpu_temp, sizeof(output.cpu_temp), cpu_temp / 1000.);
	close(fd);
#ifdef SEM
	sem_post(sem);
#endif

	THREAD_END("CPU Temp", arg)
}

/** reads /proc/stat */
void *get_cpu_util(void *arg)
{
	double user, nice, system, idle;
	double last_time_used = 0, last_time_idle = 0, cpu_util = 0;

	THREAD_START;

	FILE *f = fopen("/proc/stat", "r");
	char *line = NULL, temp[96];
	size_t len = 96;
	getline(&line, &len, f);
	sscanf(line, "%s %lf %lf %lf %lf", temp, &user, &nice, &system, &idle);
	free(line);

	if (last_time_idle > 0) {
		cpu_util =
			(user + nice + system - last_time_used) / (idle - last_time_idle);
	}

	last_time_used = user + nice + system;
	last_time_idle = idle;

#ifdef SEM
	sem_wait(sem);
#endif
	tofixed(output.cpu_usage, sizeof(output.cpu_usage), cpu_util * 100);
#ifdef SEM
	sem_post(sem);
#endif
	fclose(f);

	THREAD_END("CPU Util", arg);
}

int main(int argc, char **argv)
{
	int one_shot = 0, wait = 1;
	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i], "-1") || !strcmp(argv[i], "--one-shot")) {
			one_shot = 1;
		} else if (!strcmp(argv[i], "--no-wait")) {
			wait = 0;
		}
	}

	#define LABEL(name, value) strcpy(output.name, value);
	#define VALUE(name, value)
	#define SEP(n) strcpy(output.sep##n, SEPARATOR);
	OUTPUT
	#undef LABEL
	#undef VALUE
	#undef SEP

	#define N_THREADS 9
#ifdef SEM
	sem = sem_open("sem", O_CREAT, O_RDWR, N_THREADS);
#endif
	pthread_t threads[N_THREADS] = {
		pthread_create(&threads[0], NULL, get_gpu, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[1], NULL, get_mem, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[2], NULL, get_cpu_util, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[3], NULL, get_cpu_temp, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[4], NULL, get_cpu_speed, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[5], NULL, get_disk, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[6], NULL, get_vpn, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[7], NULL, get_ip, (void *)(wait ? 4 : 0)),
		pthread_create(&threads[8], NULL, get_date, (void *)(wait ? 1 : 0))
	};

	char buf[sizeof(output)], *ptr;
	do {
#ifdef PROFILE
		clock_t start = clock();
#endif

#ifdef SEM
		for (int i = 0 ; i < N_THREADS; ++i) sem_wait(sem);
#endif
		ptr = buf;
		for (int i = 0; i < sizeof(output); ++i) {
			char c = ((char *)&output)[i];
			if (c) {
				*ptr++ = c;
			}
		}
		write(1, buf, ptr - buf);
#ifdef SEM
		for (int i = 0 ; i < N_THREADS; ++i) sem_post(sem);
#endif

#ifdef PROFILE
		fprintf(stderr, "%s: %ld\n", "Total", clock() - start);
#endif

		if (wait) {
			sleep(1);
		}
	} while (!one_shot);

	return 0;
}
