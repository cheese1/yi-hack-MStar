/*
 * Copyright (c) 2022 roleo.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Scans the buffer and sends h264 frames to stdout.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>

#define GENERIC 0
#define H305R 1

#define RESOLUTION_LOW 360
#define RESOLUTION_HIGH 1080

#define FIFO_NAME_LOW "/tmp/h264_low_fifo"
#define FIFO_NAME_HIGH "/tmp/h264_high_fifo"

#define SIZEOF_SPS4 16
#define SIZEOF_PPS4 8
#define OFFSET_IDR4 76

#define SPS_TIMING_INFO 1

unsigned char SPS4[]              = { 0x00, 0x00, 0x00, 0x01, 0x67 };
unsigned char SPS4_1920X1080[]    = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x4D, 0x40, 0x28,
                                      0x95, 0xA0, 0x1E, 0x00, 0x89, 0xA6, 0xC0, 0x40 };
unsigned char SPS4_1920X1080_TI[] = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x4D, 0x40, 0x28,
                                      0x95, 0xA0, 0x1E, 0x00, 0x89, 0xA6, 0xC8, 0x00,
                                      0x00, 0x0F, 0xA0, 0x00, 0x02, 0x71, 0x04, 0x20 };
unsigned char SPS4_640X360[]      = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x4D, 0x40, 0x1E,
                                      0x95, 0xA0, 0x28, 0x0B, 0xFE, 0x59, 0xB0, 0x10 };
unsigned char SPS4_640X360_TI[]   = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x4D, 0x40, 0x1E,
                                      0x95, 0xA0, 0x28, 0x0B, 0xFE, 0x59, 0xB2, 0x00,
                                      0x00, 0x03, 0x03, 0xE8, 0x00, 0x00, 0x9C, 0x41,
                                      0x08 };
unsigned char SEI4_F0[]            = { 0x00, 0x00, 0x00, 0x01, 0x06, 0xF0 };
unsigned char SEI4_F0_02[]         = { 0x00, 0x00, 0x00, 0x01, 0x06, 0xF0, 0x02 };
unsigned char SEI4_F0_2C[]         = { 0x00, 0x00, 0x00, 0x01, 0x06, 0xF0, 0x2C };

unsigned char VPS5[]              = { 0x00, 0x00, 0x00, 0x01, 0x40 };

// Returns the 1st process id corresponding to pname
int pidof(const char *pname)
{
  DIR *dirp;
  FILE *fp;
  struct dirent *entry;
  char path[1024], read_buf[1024];
  int ret = 0;

  dirp = opendir ("/proc/");
  if (dirp == NULL) {
    fprintf(stderr, "error opening /proc");
    return 0;
  }

  while ((entry = readdir (dirp)) != NULL) {
    if (atoi(entry->d_name) > 0) {
      sprintf(path, "/proc/%s/comm", entry->d_name);

      /* A file may not exist, Ait may have been removed.
       * dut to termination of the process. Actually we need to
       * make sure the error is actually file does not exist to
       * be accurate.
       */
      fp = fopen (path, "r");
      if (fp != NULL) {
        fscanf (fp, "%s", read_buf);
        if (strcmp (read_buf, pname) == 0) {
            ret = atoi(entry->d_name);
            fclose (fp);
            break;
        }
        fclose (fp);
      }
    }
  }

  closedir (dirp);
  return ret;
}

// Converts virtual address to physical address
unsigned int rmm_virt2phys(unsigned int inAddr) {
    int pid;
    unsigned int outAddr;
    char sInAddr[16];
    char sMaps[1024];
    FILE *fMaps;
    char *p;
    char *line;
    size_t lineSize;

    line = (char  *) malloc(1024);

    pid = pidof("rmm");
    sprintf(sMaps, "/proc/%d/maps", pid);
    fMaps = fopen(sMaps, "r");
    sprintf(sInAddr, "%08x", inAddr);
    while (getline(&line, &lineSize, fMaps) != -1) {
        if (strncmp(line, sInAddr, 8) == 0)
            break;
    }

    p = line;
    p = strchr(p, ' ');
    p++;
    p = strchr(p, ' ');
    p++;
    p[8] = '\0';
    sscanf(p, "%x", &outAddr);
    free(line);
    fclose(fMaps);

    return outAddr;
}

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds

    return milliseconds;
}

void fillCheck(unsigned char *check, int checkSize, unsigned char *buf, int bufSize)
{
    int i;

    for (i=0; i<checkSize; i++) {
        check[i] = buf[bufSize/checkSize*i];
    }
}

void sigpipe_handler(int unused)
{
    // Do nothing
}

void print_usage(char *progname)
{
    fprintf(stderr, "\nUsage: %s [-m MODEL] [-r RES] [-d]\n\n", progname);
    fprintf(stderr, "\t-m MODEL, --model MODEL\n");
    fprintf(stderr, "\t\tset model\n");
    fprintf(stderr, "\t-r RES, --resolution RES\n");
    fprintf(stderr, "\t\tset resolution: LOW or HIGH (default HIGH)\n");
    fprintf(stderr, "\t-f, --fifo\n");
    fprintf(stderr, "\t\tenable fifo output\n");
    fprintf(stderr, "\t-t, --no-ti\n");
    fprintf(stderr, "\t\tdon't add timing info\n");
    fprintf(stderr, "\t-s, --ssf0\n");
    fprintf(stderr, "\t\tskip SEI F0\n");
    fprintf(stderr, "\t-d, --debug\n");
    fprintf(stderr, "\t\tenable debug\n");
    fprintf(stderr, "\t-h, --help\n");
    fprintf(stderr, "\t\tprint this help\n");
}

int main(int argc, char **argv)
{
    int model = GENERIC;
    int res = RESOLUTION_HIGH;
    int fifo = 0;
    int noti = 0;
    int ssf0 = 0;
    int debug = 0;

    int c;
    const char memDevice[] = "/dev/mem";
    FILE *fPtr, *fLen, *fTime, *fOut;
    int fMem;
    unsigned int ivAddr, ipAddr;
    unsigned int size;
    unsigned char *addr;
    unsigned char check1[64], check2[64];
    char filLenFile[1024];
    char timeStampFile[1024];
    unsigned char buffer[262144];
    int len;
    unsigned int time, oldTime = 0;
    int stream_started = 0;
    mode_t mode = 0755;

    while (1) {
        static struct option long_options[] =
        {
            {"model",  required_argument, 0, 'm'},
            {"resolution",  required_argument, 0, 'r'},
            {"fifo",  no_argument, 0, 'f'},
            {"no-ti",  no_argument, 0, 't'},
            {"ssf0",  no_argument, 0, 's'},
            {"debug",  no_argument, 0, 'd'},
            {"help",  no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "m:r:ftsdh",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 'm':
            if (strcasecmp("h305r", optarg) == 0) {
                model = H305R;
            } else {
                model = GENERIC;
            }
            break;

        case 'r':
            if (strcasecmp("low", optarg) == 0) {
                res = RESOLUTION_LOW;
            } else if (strcasecmp("high", optarg) == 0) {
                res = RESOLUTION_HIGH;
            }
            break;

        case 'f':
            fprintf (stderr, "using fifo as output\n");
            fifo = 1;
            break;

        case 't':
            fprintf (stderr, "don't add timing info\n");
            noti = 1;
            break;

        case 's':
            fprintf (stderr, "skip SEI F0\n");
            ssf0 = 1;
            break;

        case 'd':
            fprintf (stderr, "debug on\n");
            debug = 1;
            break;

        case 'h':
            print_usage(argv[0]);
            return -1;
            break;

        case '?':
            /* getopt_long already printed an error message. */
            break;

        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    setpriority(PRIO_PROCESS, 0, -10);

    if (debug) fprintf(stderr, "Resolution: %d\n", res);

    if (model == H305R) {
        if (res == RESOLUTION_LOW) {
            fPtr = fopen("/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_pBuffer", "r");
            fLen = fopen("/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nAllocLen", "r");
            char str[] = "h264";
            FILE *fp = fopen("/tmp/lowres", "wb");
            fwrite(str , 1 , sizeof(str) , fp);
            fclose(fp);
        } else {
            fPtr = fopen("/proc/mstar/OMX/VVHE0/ENCODER_INFO/OBUF_pBuffer", "r");
            fLen = fopen("/proc/mstar/OMX/VVHE0/ENCODER_INFO/OBUF_nAllocLen", "r");
            char str[] = "h265";
            FILE *fp = fopen("/tmp/highres", "wb");
            fwrite(str , 1 , sizeof(str) , fp);
            fclose(fp);
        }
    } else {
        if (res == RESOLUTION_LOW) {
            fPtr = fopen("/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_pBuffer", "r");
            fLen = fopen("/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_nAllocLen", "r");
            char str[] = "h264";
            FILE *fp = fopen("/tmp/lowres", "wb");
            fwrite(str , 1 , sizeof(str) , fp);
            fclose(fp);
        } else {
            fPtr = fopen("/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_pBuffer", "r");
            fLen = fopen("/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nAllocLen", "r");
            char str[] = "h264";
            FILE *fp = fopen("/tmp/highres", "wb");
            fwrite(str , 1 , sizeof(str) , fp);
            fclose(fp);
        }
    }
    if ((fPtr == NULL) || (fLen == NULL)) {
        fprintf(stderr, "unable to open /proc files\n");
        return -2;
    }

    fscanf(fPtr, "%x", &ivAddr);
    fclose(fPtr);
    fscanf(fLen, "%d", &size);
    fclose(fLen);

    ipAddr = rmm_virt2phys(ivAddr);

    if (debug) fprintf(stderr, "vaddr: 0x%08x - paddr: 0x%08x - size: %u\n", ivAddr, ipAddr, size);

    // open /dev/mem and error checking
    fMem = open(memDevice, O_RDONLY); // | O_SYNC);
    if (fMem < 0) {
        fprintf(stderr, "Failed to open the /dev/mem\n");
        return -3;
    }

    // mmap() the opened /dev/mem
    addr = (unsigned char *) (mmap(NULL, size, PROT_READ, MAP_SHARED, fMem, ipAddr));
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Failed to map memory\n");
        return -4;
    }

    // close the character device
    close(fMem);

    if (model == H305R) {
        if (res == RESOLUTION_LOW) {
            sprintf(filLenFile, "/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nFilledLen");
            sprintf(timeStampFile, "/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nTimeStamp");
        } else {
            sprintf(filLenFile, "/proc/mstar/OMX/VVHE0/ENCODER_INFO/OBUF_nFilledLen");
            sprintf(timeStampFile, "/proc/mstar/OMX/VVHE0/ENCODER_INFO/OBUF_nTimeStamp");
        }
    } else {
        if (res == RESOLUTION_LOW) {
            sprintf(filLenFile, "/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_nFilledLen");
            sprintf(timeStampFile, "/proc/mstar/OMX/VMFE1/ENCODER_INFO/OBUF_nTimeStamp");
        } else {
            sprintf(filLenFile, "/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nFilledLen");
            sprintf(timeStampFile, "/proc/mstar/OMX/VMFE0/ENCODER_INFO/OBUF_nTimeStamp");
        }
    }

    if (fifo == 0) {
        char stdoutbuf[262144];

        if (setvbuf(stdout, stdoutbuf, _IOFBF, sizeof(stdoutbuf)) != 0) {
            fprintf(stderr, "Error setting stdout buffer\n");
        }
        fOut = stdout;
    } else {
        sigaction(SIGPIPE, &(struct sigaction){sigpipe_handler}, NULL);

        if (res == RESOLUTION_LOW) {
            unlink(FIFO_NAME_LOW);
            if (mkfifo(FIFO_NAME_LOW, mode) < 0) {
                fprintf(stderr, "mkfifo failed for file %s\n", FIFO_NAME_LOW);
                return -5;
            }
            fOut = fopen(FIFO_NAME_LOW, "w");
            if (fOut == NULL) {
                fprintf(stderr, "Error opening fifo %s\n", FIFO_NAME_LOW);
                return -5;
            }
        } else if (res == RESOLUTION_HIGH) {
            unlink(FIFO_NAME_HIGH);
            if (mkfifo(FIFO_NAME_HIGH, mode) < 0) {
                fprintf(stderr, "mkfifo failed for file %s\n", FIFO_NAME_HIGH);
                return -5;
            }
            fOut = fopen(FIFO_NAME_HIGH, "w");
            if (fOut == NULL) {
                fprintf(stderr, "Error opening fifo %s\n", FIFO_NAME_HIGH);
                return -5;
            }
        }
    }

    while (1) {

        fTime = fopen(timeStampFile, "r");
        fscanf(fTime, "%u", &time);
        fclose(fTime);
        oldTime = time;

        stream_started = 0;

        while(!stream_started) {
            fTime = fopen(timeStampFile, "r");
            fscanf(fTime, "%u", &time);
            fclose(fTime);

            if (time == oldTime) {
                usleep(8000);
                continue;
            }

            usleep(1000);

            fLen = fopen(filLenFile, "r");
            fscanf(fLen, "%d", &len);
            fclose(fLen);

            memcpy(buffer, addr, len);
            oldTime = time;
            if ((memcmp(SPS4, buffer, sizeof(SPS4)) == 0) || (memcmp(VPS5, buffer, sizeof(VPS5)) == 0)) {
                if (debug) fprintf(stderr, "time: %u - len: %d\n", time, len);
                if (ssf0) {
                    if (!noti) {
                        if (memcmp(SPS4_1920X1080, buffer, sizeof(SPS4_1920X1080)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(SPS4_1920X1080_TI, 1, sizeof(SPS4_1920X1080_TI), fOut);
                            fwrite(buffer + sizeof(SPS4_1920X1080), 1, SIZEOF_PPS4, fOut);
                            fwrite(buffer + OFFSET_IDR4, 1, len - OFFSET_IDR4, fOut);
                        } else if (memcmp(SPS4_640X360, buffer, sizeof(SPS4_640X360)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(SPS4_640X360_TI, 1, sizeof(SPS4_640X360_TI), fOut);
                            fwrite(buffer + sizeof(SPS4_640X360), 1, SIZEOF_PPS4, fOut);
                            fwrite(buffer + OFFSET_IDR4, 1, len - OFFSET_IDR4, fOut);
                        } else {
                            // No change if it's a VPS
                            if (debug) fprintf(stderr, "frame is VPS\n");
                            fwrite(buffer, 1, len, fOut);
                        }
                    } else {
                        if (memcmp(SPS4, buffer, sizeof(SPS4)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(buffer, 1, SIZEOF_SPS4 + SIZEOF_PPS4, fOut);
                            fwrite(buffer + OFFSET_IDR4, 1, len - OFFSET_IDR4, fOut);
                        } else {
                            // No change if it's a VPS
                            if (debug) fprintf(stderr, "frame is VPS\n");
                            fwrite(buffer, 1, len, fOut);
                        }
                    }
                } else {
                    if (!noti) {
                        if (memcmp(SPS4_1920X1080, buffer, sizeof(SPS4_1920X1080)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(SPS4_1920X1080_TI, 1, sizeof(SPS4_1920X1080_TI), fOut);
                            fwrite(buffer + sizeof(SPS4_1920X1080), 1, len - sizeof(SPS4_1920X1080), fOut);
                        } else if (memcmp(SPS4_640X360, buffer, sizeof(SPS4_640X360)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(SPS4_640X360_TI, 1, sizeof(SPS4_640X360_TI), fOut);
                            fwrite(buffer + sizeof(SPS4_640X360), 1, len - sizeof(SPS4_640X360), fOut);
                        } else {
                            // No change if it's a VPS
                            if (debug) fprintf(stderr, "frame is VPS\n");
                            fwrite(buffer, 1, len, fOut);
                        }
                    } else {
                        if (debug) fprintf(stderr, "frame is SPS or VPS\n");
                        fwrite(buffer, 1, len, fOut);
                    }
                }
                fflush(fOut);
                stream_started = 1;
            }
        }

        while(1) {
            fTime = fopen(timeStampFile, "r");
            fscanf(fTime, "%u", &time);
            fclose(fTime);
//            if (debug) fprintf(stderr, "time: %u\n", time);

            if (time == oldTime) {
                usleep(8000);
                continue;
            } else if ((time - oldTime > 75000) && (time - oldTime <= 125000)) {
                fprintf(stderr, "frame lost: %u\n", time - oldTime);
            } else if (time - oldTime > 125000) {
                // If time - oldTime > 125000 (125 ms) assume sync lost
                fprintf(stderr, "sync lost: %u - %u\n", time, oldTime);
                break;
            }

            usleep(1000);

            fLen = fopen(filLenFile, "r");
            fscanf(fLen, "%d", &len);
            fclose(fLen);
            if (debug) fprintf(stderr, "time: %u - len: %d\n", time, len);
            if (debug) fprintf(stderr, "milliseconds: %lld\n", current_timestamp());

            if (debug) fprintf(stderr, "copy buffer: len %d\n", len);
            memcpy(buffer, addr, len);
            memset(check1, '\0', sizeof(check1));
            fillCheck(check2, sizeof(check2), buffer, len);

            while (memcmp(check1, check2, sizeof(check1)) != 0) {
                usleep(1000);
                if (debug) fprintf(stderr, "copy again buffer: len %d\n", len);
                memcpy(buffer, addr, len);
                memcpy(check1, check2, sizeof(check1));
                fillCheck(check2, sizeof(check2), buffer, len);
            }
            oldTime = time;

            if (ssf0) {
                if (memcmp(SEI4_F0_02, buffer, sizeof(SEI4_F0_02)) == 0) {
                    fwrite(buffer + 62, 1, len - 62, fOut);
                } else if (memcmp(SEI4_F0_2C, buffer, sizeof(SEI4_F0_2C)) == 0) {
                    fwrite(buffer + 52, 1, len - 52, fOut);
                } else if (memcmp(SPS4, buffer, sizeof(SPS4)) == 0) {
                    if (!noti) {
                        if (memcmp(SPS4_1920X1080, buffer, sizeof(SPS4_1920X1080)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(SPS4_1920X1080_TI, 1, sizeof(SPS4_1920X1080_TI), fOut);
                            fwrite(buffer + sizeof(SPS4_1920X1080), 1, SIZEOF_PPS4, fOut);
                            fwrite(buffer + OFFSET_IDR4, 1, len - OFFSET_IDR4, fOut);
                        } else if (memcmp(SPS4_640X360, buffer, sizeof(SPS4_640X360)) == 0) {
                            if (debug) fprintf(stderr, "frame is SPS\n");
                            fwrite(SPS4_640X360_TI, 1, sizeof(SPS4_640X360_TI), fOut);
                            fwrite(buffer + sizeof(SPS4_640X360), 1, SIZEOF_PPS4, fOut);
                            fwrite(buffer + OFFSET_IDR4, 1, len - OFFSET_IDR4, fOut);
                        }
                    } else {
                        fwrite(buffer, 1, 24, fOut);
                        fwrite(buffer + 76, 1, len - 76, fOut);
                    }
                } else if (memcmp(VPS5, buffer, sizeof(VPS5)) == 0) {
                    if (debug) fprintf(stderr, "frame is VPS\n");
                    fwrite(buffer, 1, len, fOut);
                } else {
                    fwrite(buffer, 1, len, fOut);
                }
            } else {
                if (!noti) {
                    if (memcmp(SPS4_1920X1080, buffer, sizeof(SPS4_1920X1080)) == 0) {
                        if (debug) fprintf(stderr, "frame is SPS\n");
                        fwrite(SPS4_1920X1080_TI, 1, sizeof(SPS4_1920X1080_TI), fOut);
                        fwrite(buffer + sizeof(SPS4_1920X1080), 1, len - sizeof(SPS4_1920X1080), fOut);
                    } else if (memcmp(SPS4_640X360, buffer, sizeof(SPS4_640X360)) == 0) {
                        if (debug) fprintf(stderr, "frame is SPS\n");
                        fwrite(SPS4_640X360_TI, 1, sizeof(SPS4_640X360_TI), fOut);
                        fwrite(buffer + sizeof(SPS4_640X360), 1, len - sizeof(SPS4_640X360), fOut);
                    } else if (memcmp(VPS5, buffer, sizeof(VPS5)) == 0) {
                        if (debug) fprintf(stderr, "frame is VPS\n");
                        fwrite(buffer, 1, len, fOut);
                    } else {
                        fwrite(buffer, 1, len, fOut);
                    }
                } else {
                    fwrite(buffer, 1, len, fOut);
                }
            }
            fflush(fOut);
        }
    }

    if (fifo == 1) {
        if (res == RESOLUTION_LOW) {
            fclose(fOut);
            unlink(FIFO_NAME_LOW);
        } else if (res == RESOLUTION_HIGH) {
            fclose(fOut);
            unlink(FIFO_NAME_HIGH);
        }
    }

    munmap(addr, size);

    return 0;
}
