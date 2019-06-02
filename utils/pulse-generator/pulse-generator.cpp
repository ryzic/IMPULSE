/*
 * pulse-generator.cpp
 *
 * Created on: Apr 9, 2019
 * Copyright (C) 2019  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sched.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#define USECS_PER_SEC 1000000
#define SECS_PER_MINUTE 60
#define SECS_PER_DAY 86400
#define ON_TIME 3
#define DELAYED 1
#define NONE 2
#define GPIO_A 0
//#define O_RDWR 02
//#define GPIO_B 1
#define WRITE_DELAY 2						// Estimated delay between write request and
								// assertion of output pin for Raspberry Pi 3.
#define JITTER_DISTRIB_LEN 61
#define SETTLE_TIME 10

const char *version = "pulse-generator v1.0.1";

const char *p1_distrib_file = "/var/local/pulse1-distrib-forming";
const char *last_p1_distrib_file = "/var/local/pulse1-distrib";

int sysCommand(const char *cmd){
	int rv = system(cmd);
	if (rv == -1 || WIFEXITED(rv) == false){
		printf("System command failed: %s\n", cmd);
		return -1;
	}
	return 0;
}

struct pulseGeneratorGlobalVars {
	int seq_num;
	char strbuf[200];
	char msgbuf[200];
	int pulseTime1;
	//int pulseTime2;
	bool badRead;

	int p1Count;
	int p1Distrib[JITTER_DISTRIB_LEN];
	int lastP1Fileno;

	
} g;

/**
 * Constructs an error message.
 */
void couldNotOpenMsgTo(char *msgbuf, const char *filename){
	strcpy(msgbuf, "ERROR: could not open \"");
	strcat(msgbuf, filename);
	strcat(msgbuf, "\": ");
	strcat(msgbuf, strerror(errno));
	strcat(msgbuf, "\n");
}

/**
 * Opens a file with error printing and sets standard
 * file permissions for O_CREAT.
 *
 * @param[in] filename The file to open.
 * @param[in] flags The file open flags.
 *
 * @returns The file descriptor.
 */
int open_logerr(const char* filename, int flags){
	mode_t mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
	int fd;
	//printf("OK");
	if ((flags & O_CREAT) == O_CREAT){
		fd = open(filename, flags, mode);
		if (fd == -1){
			couldNotOpenMsgTo(g.msgbuf, filename);
			printf("%s\n", g.msgbuf);
			return -1;
		}
	}
	else {
		fd = open(filename, flags);
		if (fd == -1){
			couldNotOpenMsgTo(g.msgbuf, filename);
			printf("%s\n", g.msgbuf);
			return -1;
		}
	}
	return fd;
}

/**
 * Writes an accumulating statistical distribution to disk and
 * rolls over the accumulating data to a new file every epoch
 * counts and begins a new distribution file. An epoch is
 * 86,400 counts.
 *
 * @param[in] distrib The int array containing the distribution.
 * @param[in] len The length of the array.
 * @param[in] scaleZero The array index corresponding to distribution zero.
 * @param[in] count The current number of samples in the distribution.
 * @param[out] last_epoch The saved count of the previous epoch.
 * @param[in] distrib_file The filename of the last completed
 * distribution file.
 * @param[in] last_distrib_file The filename of the currently
 * forming distribution file.
 */
void writeDistribution(int distrib[], int len, int scaleZero, int count,
		int *last_epoch, const char *distrib_file, const char *last_distrib_file){
	int rv;
	remove(distrib_file);
	int fd = open_logerr(distrib_file, O_CREAT | O_WRONLY | O_APPEND);
	if (fd == -1){
		return;
	}
	for (int i = 0; i < len; i++){
		sprintf(g.strbuf, "%d %d\n", i-scaleZero, distrib[i]);
		rv = write(fd, g.strbuf, strlen(g.strbuf));
		if (rv == -1){
			printf("Write to %s failed.\n", distrib_file);
			break;
		}
	}
	close(fd);

	int epoch = count / SECS_PER_DAY;
	if (epoch != *last_epoch ){
		*last_epoch = epoch;
		remove(last_distrib_file);
		rename(distrib_file, last_distrib_file);
		memset(distrib, 0, len * sizeof(int));
	}
}

/**
 * Writes a distribution to disk approximately once a minute
 * containing 60 additional jitter samples recorded at the
 * occurrance of pulse1. The distribution is rolled over to
 * a new file every 24 hours.
 */
void writeP1JitterDistribFile(void){
	if (g.p1Count % SECS_PER_MINUTE == 0){
		int scaleZero = JITTER_DISTRIB_LEN / 6;
		writeDistribution(g.p1Distrib, JITTER_DISTRIB_LEN, scaleZero, g.p1Count,
				&g.lastP1Fileno, p1_distrib_file, last_p1_distrib_file);
	}
}

/**
 * Constructs a distribution of relative pulse time
 * relative to pulseVal that can be saved to disk
 * for analysis.
 *
 * @param[in] pulseTime The time value to save to
 * the distribution.
 *
 * @param[in] pulseVal The requested pulse value
 * used as the time reference.
 *
 * @param[in/out] distrib The pulse distribution to create.
 *
 * @param[in/out] count The count of recorded pulse times.
 */
void buildPulseDistrib(int pulseTime, int pulseVal, int distrib[], int *count){
	if (g.seq_num <= SETTLE_TIME){
		return;
	}
	int len = JITTER_DISTRIB_LEN - 1;
	int idx = (pulseTime - pulseVal) + len / 6;

	if (idx < 0){
		idx = 0;
	}
	else if (idx > len){
		idx = len;
	}
	distrib[idx] += 1;
	//printf("OK");
	*count += 1;
}

/**
 * Reads the major number assigned to pulse-generator
 * from "/proc/devices" as a string which is
 * returned in the majorPos char pointer. This
 * value is used to load the hardware driver that
 * pulse-generator requires.
 */
char *copyMajorTo(char *majorPos){

	struct stat stat_buf;

	const char *filename = "/run/shm/proc_devices";

	int rv = sysCommand("cat /proc/devices > /run/shm/proc_devices"); 	// "/proc/devices" can't be handled like
	if (rv == -1){								// a normal file so we copy it to a file.
		return NULL;
	}

	int fd = open(filename, O_RDONLY);
	if (fd == -1){
		return NULL;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	char *fbuf = new char[sz+1];

	rv = read(fd, fbuf, sz);
	if (rv == -1){
		close(fd);
		remove(filename);
		delete(fbuf);
		return NULL;
	}
	close(fd);
	remove(filename);

	fbuf[sz] = '\0';

	char *pos = strstr(fbuf, "pulse-generator");
	if (pos == NULL){
		printf("Can't find pulse-generator in \"/run/shm/proc_devices\"\n");
		delete fbuf;
		return NULL;
	}
	char *end = pos - 1;
	*end = 0;

	pos -= 2;
	char *pos2 = pos;
	while (pos2 == pos){
		pos -= 1;
		pos2 = strpbrk(pos,"0123456789");
	}
	strcpy(majorPos, pos2);

	delete fbuf;
	return majorPos;
}

/**
 * Loads the hardware driver required by pulse-generator which
 * is expected to be available in the file:
 * "/lib/modules/'uname -r'/kernel/drivers/misc/pulse-generator.ko".
 */
int driver_load(char *gpio1){  
	//printf("OK");
	memset(g.strbuf, 0, 200 * sizeof(char));

	char *insmod = g.strbuf;

	strcpy(insmod, "/sbin/insmod /lib/modules/`uname -r`/kernel/drivers/misc/pulse-generator.ko gpio_num1=");
	strcat(insmod, gpio1);
	//printf("OK");
	

	int rv = sysCommand("rm -f /dev/pulse-generator");			// Clean up any old device files.
	if (rv == -1){
		return -1;
	}

	rv = sysCommand(insmod);										// Issue the insmod command
	if (rv == -1){
		return -1;
	}


	char *mknod = g.strbuf;
	//printf("OK2");
	strcpy(mknod, "mknod /dev/pulse-generator c ");
	char *major = copyMajorTo(mknod + strlen(mknod));
	if (major == NULL){								// No major found! insmod failed.
		printf("driver_load() error: No major found!\n");
		sysCommand("/sbin/rmmod pulse-generator");
		return -1;
	}
	strcat(mknod, " 0");

	rv = sysCommand(mknod);									// Issue the mknod command
	if (rv == -1){
		return -1;
	}

	rv = sysCommand("chgrp root /dev/pulse-generator");
	if (rv == -1){
		return -1;
	}
	rv = sysCommand("chmod 664 /dev/pulse-generator");
	if (rv == -1){
		return -1;
	}
	//printf("OK2");
	return 0;
}

/**
 * Unloads the pulse-generator kernel driver.
 */
void driver_unload(void){
	sysCommand("/sbin/rmmod pulse-generator");
	sysCommand("rm -f /dev/pulse-generator");
}

/**
 * Sets a nanosleep() time delay equal to the time remaining
 * in the second from the time recorded as fracSec plus an
 * adjustment value of timeAt in microseconds.
 */
struct timespec setSyncDelay(int timeAt, int fracSec){

	struct timespec ts2;

	int timerVal = USECS_PER_SEC + timeAt - fracSec;

	if (timerVal >= USECS_PER_SEC){
		ts2.tv_sec = 1;
		ts2.tv_nsec = (timerVal - USECS_PER_SEC) * 1000;
	}
	else if (timerVal < 0){
		ts2.tv_sec = 0;
		ts2.tv_nsec = (USECS_PER_SEC + timerVal) * 1000;
	}
	else {
		ts2.tv_sec = 0;
		ts2.tv_nsec = timerVal * 1000;
	}

	return ts2;
}

void writePulseStatus(int readData[], int pulseTime){
	if (g.badRead){
		printf("pulse-gemerator: Bad read from driver\n");
		return;
	}

	int pulseEnd = readData[1];
	if (pulseEnd > pulseTime){
		printf("pulse-gemerator: Omitting pulse delayed by system latency.\n");
	}
}
 //printf("OK");
/**
 * Generates one or two once-per-second pulses at the
 * microsecond offsets given on the command line.
 */
int main(int argc, char *argv[]){

	int pulseStart1 = 0; 
	int writeData[2];
	int readData[2];
	
	struct timeval tv1;
	struct timespec ts2;
	int pulseEnd1 = 0;

	memset(&g, 0, sizeof(struct pulseGeneratorGlobalVars));
	g.pulseTime1 = -1;
	
	g.badRead = false;
	//printf("OK");
	if (argc > 1){
		//printf("OK");
		if (strcmp(argv[1], "load-driver") == 0){
			if (geteuid() != 0){
				printf("Requires superuser privileges. Please sudo this command.\n");
				return 1;
			}

			if (argv[2] == NULL){
				printf("GPIO number is a required second arg.\n");
				printf("Could not load driver.\n");
				return 1;
			}

			if (argc == 4){
				//printf("OK");
				if (driver_load(argv[2]) == -1){
					printf("Could not load pulse-generator driver. Exiting.\n");
					return 1;
				}
			}
			else {
				if (driver_load(argv[2]) == -1){
					printf("Could not load pulse-generator driver. Exiting.\n");
					return 1;
				}
			}

			printf("pulse-generator: driver loaded\n");
			return 0;
		}
		if (strcmp(argv[1], "unload-driver") == 0){
			if (geteuid() != 0){
				printf("Requires superuser privileges. Please sudo this command.\n");
				return 1;
			}

			printf("pulse-generator: driver unloaded\n");
			driver_unload();
			return 0;
		}
		if (strcmp(argv[1], "-p") == 0 && argc == 3){
			sscanf(argv[2], "%d", &g.pulseTime1);
			if (g.pulseTime1 >= 0){
				//printf("OK");
				goto start;
			}
		}
		if (strcmp(argv[1], "-p") == 0 && argc == 4){
			sscanf(argv[2], "%d", &g.pulseTime1);
			

			if (g.pulseTime1 >= 0 ){
				goto start;
			}
		}

		printf("Usage:\n");
		printf("Load driver with one or two output GPIOs:\n");
		printf("  sudo pulse-generator load-driver <gpio-num1> [gpio-num2]\n");
		printf("After loading the driver, calling pulse-generator\n");
		printf("with the following flag and value(s) causes it to\n");
		printf("generate one or two once-per-second pulse(s) at\n");
		printf("specified time(s) offset from the rollover of the\n");
		printf("second:\n");
		printf("  -p <microseconds> [microseconds]\n");
		printf("When the driver is no longer needed:\n");
		printf("  sudo pulse-generator unload-driver\n");

		return 0;
	}

start:
	char timeStr[100];
	const char *timefmt = "%F %H:%M:%S";
	int rv;
	const char *deviceName = "/dev/pulse-generator";

	if (geteuid() != 0){
		printf("Requires superuser privileges. Please sudo this command.\n");
		return 1;
	}

	printf("%s\n", version);

	struct sched_param param;							// Process must be run as
	param.sched_priority = 99;							// root to change priority.
	sched_setscheduler(0, SCHED_FIFO, &param);			// Else, this has no effect.

	int fd = open(deviceName, O_RDWR);		// Open the pulse-generator device driver.
	if (fd == -1){
		printf("pulse-generator: Driver is not loaded. Exiting.\n");
		return 1;
	}
	
	//int latency = 200;

	//pulseStart1 = g.pulseTime1 - latency;				// This will start the driver about 200 microsecs ahead of the
														// pulse write time thus allowing about 50 usec coming out of sleep
														// plus 150 usecs of system response latency. A spin loop in the
														// driver will chew up the excess time until the write at g.pulseTime1.
	gettimeofday(&tv1, NULL);
	//ts2 = setSyncDelay(pulseStart1, (tv1.tv_usec));		// Sleep to pulseStart1

	for (;;){
		
		//nanosleep(&ts2, NULL);
		//printf("OK");
		writeData[0] = GPIO_A;							// Identify the first GPIO outuput.
		writeData[1] = ((g.pulseTime1 - WRITE_DELAY) - 100) ;		// Set the pulse time.
		rv = write(fd, writeData, 2 * sizeof(int));		// Request a write at g.pulseTime1.
		if (rv == -1){
			printf("Write to %s failed.\n", deviceName);
			break;
		}

		if (read(fd, readData, 2 * sizeof(int)) < 0){	// Read the time the write actually occurred.
			g.badRead = true;
		}
		else {
			readData[1] += WRITE_DELAY;
			pulseEnd1 = readData[1];
			//printf("OK");
			//buildPulseDistrib(pulseEnd1, g.pulseTime1, g.p1Distrib, &g.p1Count);
		}

		//writePulseStatus(readData, g.pulseTime1);
		
		strftime(timeStr, 100, timefmt, localtime((const time_t*)(&(readData[0]))));
		printf("%s %d\n", timeStr, pulseEnd1);
		//printf("%s %d\n", timeStr);
		
		g.badRead = false;

		g.seq_num += 1;
		
		gettimeofday(&tv1, NULL);
		//ts2 = setSyncDelay(pulseStart1, tv1.tv_usec);	// Sleep to pulseStart1.
		//printf("OK");
	}

	close(fd);											// Close the pulse-generator device driver.

	return 0;
}
