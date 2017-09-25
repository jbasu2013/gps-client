/*
 * Author: Daniel Maxime (root@maxux.net)
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

//
// bundle stuff
//
typedef struct bundle_t {
    int count;
    size_t maxsize;
    char *writer;
    char *buffer;

} bundle_t;

// reset bundle pointer and size
void bundle_reset(bundle_t *bundle) {
    bundle->count = 0;
    bundle->writer = bundle->buffer;
}

// return current bundle content-size in bytes
size_t bundle_length(bundle_t *bundle) {
    return bundle->writer - bundle->buffer;
}

// initialize empty bundle
void bundle_init(bundle_t *bundle) {
    bundle->maxsize = 8192;
    bundle->buffer = (char *) malloc(sizeof(char) * bundle->maxsize);
    bundle_reset(bundle);
}

// append a line to the bundle
int bundle_append(bundle_t *bundle, char *line) {
    size_t curlen = bundle_length(bundle);
    size_t linelen = strlen(line);

    if(curlen + linelen + 1 > bundle->maxsize) {
        printf("[-] bundle overflow, skipping\n");
        return -1;
    }

    // append the line to the buffer
    sprintf(bundle->writer, "%s\n", line);

    // updating pointer
    bundle->writer += linelen + 1;
    bundle->count += 1;

    return bundle->count;
}

//
// error handling
//
static void diep(char *str) {
    fprintf(stderr, "[-] %s: [%d] %s\n", str, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

static void dier(char *str) {
    fprintf(stderr, "[-] %s\n", str);
    exit(EXIT_FAILURE);
}

static int errp(char *str) {
    perror(str);
    return -1;
}

//
// device setter
//
static int configurefd(int fd, int speed) {
    struct termios tty;

    memset(&tty, 0, sizeof(tty));

    if(tcgetattr(fd, &tty) != 0)
        diep("tcgetattr");

    tty.c_cflag = speed | CRTSCTS | CS8 | CLOCAL | CREAD;

    tty.c_iflag  = IGNPAR | ICRNL;
    tty.c_lflag  = ICANON;
    tty.c_oflag  = 0;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    if(tcsetattr(fd, TCSANOW, &tty) != 0)
        diep("tcgetattr");

    return 0;
}

//
// device io
//
char *readfd(int fd, char *buffer, size_t length) {
    int res, saved = 0;
    fd_set readfs;
    int selval;
    char *temp;

    FD_ZERO(&readfs);

    while(1) {
        FD_SET(fd, &readfs);

        if((selval = select(fd + 1, &readfs, NULL, NULL, NULL)) < 0)
            diep("select");

        if(FD_ISSET(fd, &readfs)) {
            if((res = read(fd, buffer + saved, length - saved)) < 0)
                diep("fd read");

            buffer[res + saved] = '\0';

            // line/block is maybe not completed, waiting for a full line/block
            if(buffer[res + saved - 1] != '\n') {
                saved = res;
                continue;
            }

            buffer[res + saved - 1] = '\0';

            for(temp = buffer; *temp == '\n'; temp++);
            memmove(buffer, temp, strlen(temp));

            if(!*buffer)
                continue;
        }

        return buffer;
    }
}

//
// network io
//
int net_connect(char *host, int port) {
	int sockfd;
	struct sockaddr_in addr_remote;
	struct hostent *hent;

	/* create client socket */
	addr_remote.sin_family = AF_INET;
	addr_remote.sin_port = htons(port);

	/* dns resolution */
	if((hent = gethostbyname(host)) == NULL)
		return errp("[-] gethostbyname");

	memcpy(&addr_remote.sin_addr, hent->h_addr_list[0], hent->h_length);

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		return errp("[-] socket");

	/* connecting */
	if(connect(sockfd, (const struct sockaddr *) &addr_remote, sizeof(addr_remote)) < 0)
		return errp("[-] connect");

	return sockfd;
}

//
// network request
//
char *http(char *endpoint) {
    char *server = "home.maxux.net";
    // char *server = "10.241.0.18";
    int port = 5555;

    char header[2048];
    char buffer[256];
    int sockfd;
    int length;

    printf("[+] pushing data...\n");
    fflush(stdout);

    sprintf(header, "GET %s\r\n\r\n", endpoint);

    if((sockfd = net_connect(server, port)) < 0)
        return NULL;

    if(send(sockfd, header, strlen(header), 0) < 0) {
        perror("[-] send");
        return NULL;
    }

    if((length = recv(sockfd, buffer, sizeof(buffer), 0)) < 0)
        perror("[-] read");

    close(sockfd);

    buffer[length] = '\0';

    printf("[+] response: %s\n", buffer);
    fflush(stdout);

    return strdup(buffer);
}

char *post(char *endpoint, bundle_t *bundle) {
    char *server = "home.maxux.net";
    // char *server = "10.241.0.18";
    int port = 5555;

    char frame[8192];
    int sockfd;
    int length;

    printf("[+] pushing data...\n");

    char *header = "POST %s HTTP/1.0\r\n"
                   "Content-Length: %lu\r\n"
                   "\r\n%s";

                   //      url       content-length         body
    sprintf(frame, header, endpoint, bundle_length(bundle), bundle->buffer);

    if((sockfd = net_connect(server, port)) < 0)
        return NULL;

    if(send(sockfd, frame, strlen(frame), 0) < 0) {
        perror("[-] send");
        return NULL;
    }

    if((length = recv(sockfd, frame, sizeof(frame), 0)) < 0)
        perror("[-] read");

    close(sockfd);

    frame[length] = '\0';

    printf("[+] response: %s\n", frame);

    return strdup(frame);
}

//
// local logs
//
int logs_create() {
    char filename[256];
    int fd;

    sprintf(filename, "/mnt/backlog/gps-%lu", time(NULL));

    if((fd = open(filename, O_WRONLY | O_CREAT, 0644)) < 0)
        return errp(filename);

    return fd;
}

void logs_append(int fd, char *line) {
    if(write(fd, line, strlen(line)) < 0)
        perror("[-] logs write");

    if(write(fd, "\n", 1) < 0)
        perror("[-] logs write");
}

//
// main worker
//
int main(void) {
    int fd, logsfd;
    char *device = "/dev/ttyAMA0";
    char buffer[2048];
    char *response = NULL;
    bundle_t bundle;

    // local logs
    printf("[+] opening local log file\n");
    if((logsfd = logs_create()) < 0)
        diep("[-] logs");

    // bundle buffer
    bundle_init(&bundle);

    // connecting to the network
    while(!(response = http("/api/ping"))) {
        printf("[-] could not reach endpoint, waiting\n");
        usleep(1000000);
    }

    if(strncmp("{\"pong\"", response, 7))
        dier("wrong pong response from server");

    free(response);

    // setting up serial console
    if((fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
        diep(device);

    configurefd(fd, B9600);

    // starting a new session
    if(!(response = http("/api/push/session")))
        dier("cannot create new session");

    while(1) {
        printf("[+] waiting serial data\n");
        readfd(fd, buffer, sizeof(buffer));

        printf("[+] >> %s\n", buffer);

        // skip invalid header
        if(buffer[0] != '$')
            continue;

        // saving to local logs
        logs_append(logsfd, buffer);

        // bundle the line
        if(bundle_append(&bundle, buffer) < 0) {
            // should not happen
            bundle_reset(&bundle);
            continue;
        }

        // sending when RMC frame is received
        if(!strncmp(buffer, "$GPRMC", 6)) {
            // sending bundle over the network
            if(!(response = post("/api/push/datapoint", &bundle)))
                printf("[-] cannot send datapoint\n");

            bundle_reset(&bundle);
        }
    }

    return 0;
}
