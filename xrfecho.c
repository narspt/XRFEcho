/*
    XrfEcho - DExtra Echo
    Copyright (C) 2021 Nuno Silva

    Based on code from https://github.com/nostar/reflector_connectors
    Copyright (C) 2019 Doug McLain

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h> 
#include <stdlib.h>
#include <signal.h>
#include <unistd.h> 
#include <string.h> 
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <sys/ioctl.h>

#define CLIENT_MOD 'G'
#define RCD_MAX_TIME 240
#define RCD_DELAY 3
//#define XRF1_DXRFD_COMPAT
#define BUFSIZE 2048
#define TIMEOUT 30
//#define DEBUG_SEND
//#define DEBUG_RECV

char 		*ref1;
int 		udp1;
fd_set 		udpset; 
struct 		sockaddr_in host1;
uint8_t 	buf[BUFSIZE];
char    	callsign[8U];
uint8_t 	host1_connect;

void process_signal(int sig)
{
	if(sig == SIGINT){
		fprintf(stderr, "\n\nShutting down link\n");
		memcpy(buf, callsign, 8);
		buf[8] = CLIENT_MOD;
		buf[9] = ' ';
		buf[10] = 0x00;
		sendto(udp1, buf, 11, 0, (const struct sockaddr *)&host1, sizeof(host1));
#ifdef DEBUG_SEND
		fprintf(stderr, "SEND %s: ", ref1);
		for(int i = 0; i < 11; ++i){
			fprintf(stderr, "%02x ", buf[i]);
		}
		fprintf(stderr, "\n");
		fflush(stderr);
#endif
		close(udp1);
		exit(EXIT_SUCCESS);
	}
	if(sig == SIGALRM){
		memcpy(buf, callsign, 8);
		buf[8] = 0x00;
		sendto(udp1, buf, 9, 0, (const struct sockaddr *)&host1, sizeof(host1));
#ifdef DEBUG_SEND
		fprintf(stderr, "SEND %s: ", ref1);
		for(int i = 0; i < 9; ++i){
			fprintf(stderr, "%02x ", buf[i]);
		}
		fprintf(stderr, "\n");
		fflush(stderr);
#endif
		alarm(5);
	}
}

int main(int argc, char **argv)
{
	struct sockaddr_in rx;
	struct hostent *hp;
	struct timeval tv;
	char *mod1;
	char *host1_url;
	int host1_port;
	socklen_t l = sizeof(host1);
	int rxlen;
	int r;
	uint16_t streamid = 0;
	const uint8_t header[4] = {0x44,0x53,0x56,0x54}; 	//packet header
	time_t pingt = 0;

	uint8_t rcd_hdr[56];
	uint8_t rcd_frms[50*RCD_MAX_TIME][27];
	int rcd_fcnt = 0;
	time_t rcd_endt = 0;

	if(argc != 3){
		fprintf(stderr, "Usage: xrfecho [CALLSIGN] [XRFName:MOD:XRFHostIP:PORT]\n");
		return 0;
	}
	else{
		memset(callsign, ' ', 8);
		memcpy(callsign, argv[1], (strlen(argv[1])<8)?strlen(argv[1]):8);
		
		ref1 = strtok(argv[2], ":");
		mod1 = strtok(NULL, ":");
		host1_url = strtok(NULL, ":");
		host1_port = atoi(strtok(NULL, ":"));
		
		printf("XRF: %s%c %s:%d\n", ref1, mod1[0], host1_url, host1_port);
	}
	
	signal(SIGINT, process_signal); 						//Handle CTRL-C gracefully
	signal(SIGALRM, process_signal); 						//Watchdog
	
	if ((udp1 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket\n");
		return 0;
	}

#ifdef XRF1_DXRFD_COMPAT
	memset((char *)&host1, 0, sizeof(host1));
	host1.sin_family = AF_INET;
	host1.sin_port = htons(30001);
	host1.sin_addr.s_addr = htonl(INADDR_ANY);
	if ( bind(udp1, (struct sockaddr *)&host1, sizeof(host1)) == -1 ) {
		fprintf(stderr, "error while binding the socket on port 30001\n");
		return 0;
	}
#endif

	memset((char *)&host1, 0, sizeof(host1));
	host1.sin_family = AF_INET;
	host1.sin_port = htons(host1_port);

	hp = gethostbyname(host1_url);
	if (!hp) {
		fprintf(stderr, "could not resolve %s\n", host1_url);
		return 0;
	}
	memcpy((void *)&host1.sin_addr, hp->h_addr_list[0], hp->h_length);
	
	host1_connect = 1;
	alarm(5);

	while (1) {
		if(host1_connect){
			host1_connect = 0;
			pingt = time(NULL);
			memcpy(buf, callsign, 8);
			buf[8] = CLIENT_MOD;
			buf[9] = mod1[0];
			buf[10] = 0x00;
			sendto(udp1, buf, 11, 0, (const struct sockaddr *)&host1, sizeof(host1));
			fprintf(stderr, "Connecting to %s...\n", ref1);
#ifdef DEBUG_SEND
			fprintf(stderr, "SEND %s: ", ref1);
			for(int i = 0; i < 11; ++i){
				fprintf(stderr, "%02x ", buf[i]);
			}
			fprintf(stderr, "\n");
			fflush(stderr);
#endif
		}
		FD_ZERO(&udpset);
		FD_SET(udp1, &udpset);
		tv.tv_sec = 0;
		tv.tv_usec = 100*1000;
		r = select(udp1 + 1, &udpset, NULL, NULL, &tv);
		//fprintf(stderr, "Select returned r == %d\n", r);
		rxlen = 0;
		if(r > 0){
			rxlen = recvfrom(udp1, buf, BUFSIZE, 0, (struct sockaddr *)&rx, &l);
		}
#ifdef DEBUG_RECV
		if(rxlen){
			fprintf(stderr, "RECV %s: ", ref1);
			for(int i = 0; i < rxlen; ++i){
				fprintf(stderr, "%02x ", buf[i]);
			}
			fprintf(stderr, "\n");
			fflush(stderr);
		}
#endif
		if(rxlen == 9){ //keep-alive
			pingt = time(NULL);
		}
		if((rxlen == 56) && (!memcmp(&buf[0], header, 4))) { //dv header
			if( (rx.sin_addr.s_addr == host1.sin_addr.s_addr) && !memcmp(&buf[18], ref1, 6) && (buf[25] == mod1[0]) ){
				uint16_t s = (buf[12] << 8) | (buf[13] & 0xff);
				if(s != streamid){
					streamid = s;
					memcpy(rcd_hdr, buf, 56);
					rcd_fcnt = 0;
					rcd_endt = 0;
				}
			}
		}
		if(rxlen == 27){ //dv frame
			if( (rx.sin_addr.s_addr == host1.sin_addr.s_addr) ){
				uint16_t s = (buf[12] << 8) | (buf[13] & 0xff);
				if(s == streamid){
					if (rcd_fcnt < 50*RCD_MAX_TIME)
						memcpy(rcd_frms[rcd_fcnt++], buf, 27);
					//if ((buf[14] & 0x40) != 0) //last frame
						rcd_endt = time(NULL);
				}
			}
		}
		if (rcd_endt && (time(NULL)-rcd_endt > RCD_DELAY)) { //playback
			rcd_endt = 0;
			streamid = 0;
#ifdef DEBUG_SEND
			fprintf(stderr, "Starting playback of %d frames...\n", rcd_fcnt);
			fflush(stderr);
#endif

			for (int i = 0; i < 5; i++)
				sendto(udp1, rcd_hdr, 56, 0, (const struct sockaddr *)&host1, sizeof(host1));
			
			struct timespec nanos;
			int64_t nowus, trgus = 0;
			for (int i = 0; i < rcd_fcnt; i++) {
				sendto(udp1, rcd_frms[i], 27, 0, (const struct sockaddr *)&host1, sizeof(host1));

				clock_gettime(CLOCK_MONOTONIC, &nanos);
				nowus = (int64_t)nanos.tv_sec * 1000000 + nanos.tv_nsec / 1000;

				if (abs(trgus - nowus) > 1000000)
					trgus = nowus;
				trgus += 20000;

				if (trgus > nowus) {
					nanos.tv_sec = (trgus - nowus) / 1000000;
					nanos.tv_nsec = (trgus - nowus) % 1000000 * 1000;
					while (nanosleep(&nanos, &nanos) == -1 && errno == EINTR);
				}
			}

#ifdef DEBUG_SEND
			fprintf(stderr, "Finished playback.\n");
			fflush(stderr);
#endif
			pingt = time(NULL); //pingt was not updated during playback
		}
		if (time(NULL)-pingt > TIMEOUT) { //connection timeout
			host1_connect = 1;
			fprintf(stderr, "%s ping timeout\n", ref1);
		}
	}
}
