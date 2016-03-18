/*
 * client.c
 * 
 * Copyright 2016 Che Hongwei <htc.chehw@gmail.com>
 * 
 * The MIT License (MIT)
 * 
 * Permission is hereby granted, free of charge, to any person 
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, merge, 
 * publish, distribute, sublicense, and/or sell copies of the Software, 
 * and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR 
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include <unistd.h>
#include <fcntl.h>


static int chutil_make_non_blocking(int fd)
{
	int rc;
	int flags;
	flags = fcntl(fd, F_GETFL, 0);
	if(-1 == flags)
	{
		perror("fcntl");
		return -1;
	}
	
	flags |= O_NONBLOCK;
	rc = fcntl(fd, F_SETFL, flags);
	if(-1 == rc)
	{
		perror("fcntl");
		return -1;
	}
	return 0;
}



static volatile int quit = 0;

static void * worker_thread(void * param);
static void * input_thread(void * param);
static int client_run(const char * serv_name, const char * port);

int main(int argc, char **argv)
{
	pthread_t th[2];
	
	pthread_create(&th[0], NULL, worker_thread, NULL);
	pthread_create(&th[1], NULL, input_thread, NULL);
	
	
	pthread_join(th[0], NULL);
	return 0;
}

static void * worker_thread(void * param)
{	
	client_run(NULL, NULL);
	
	pthread_exit(0);
}


static void * input_thread(void * param)
{
	int c;
	printf("press 'q' to quit.\n");
	while(!quit)
	{
		c = getchar();
		if(c == '\n') continue;
		if(c == 'q' || c == 'Q') 
		{
			quit = 1;
			break;
		}
	}
	
	pthread_exit(0);
}



#define SATOSHI_MAGIC_MAIN 		(0xD9B4BEF9)
#define SATOSHI_MAGIC_TESTNET3 	(0x0709110B)
typedef struct satoshi_msg_header
{
	uint32_t magic;
	char command[12];
	uint32_t length;
	uint32_t checksum;
}satoshi_msg_header_t;

static int client_run(const char * serv_name, const char * port)
{
	int fd;
	int rc;
	struct addrinfo hints, * serv_info, * p;
	
	if(NULL == serv_name) serv_name = "127.0.0.1";
	if(NULL == port) port = "43690";
	
	int i;
	#define ROUNDS 3
	int fds[ROUNDS];
	for(i = 0; i < ROUNDS; ++i)
		fds[i] = -1;
	
	
	satoshi_msg_header_t msg_hdr;
	memset(&msg_hdr, 0, sizeof(msg_hdr));
	msg_hdr.magic = SATOSHI_MAGIC_TESTNET3;
	strcpy(msg_hdr.command, "version");
	
		
	for(i = 0; i < ROUNDS; ++i)
	{
		char buf[512];
		ssize_t cb;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		//~ hints.ai_flags = AI_PASSIVE;
		
		rc = getaddrinfo(serv_name, port, &hints, &serv_info);
		if(0 != rc)
		{
			fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(rc));
			return -1;
		}
		for(p = serv_info; p != NULL; p = p->ai_next)
		{
			fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if(-1 != fd) break;
		}
		
		if(NULL == p)
		{
			fprintf(stderr, "ERROR: Could not connect to %s:%s.\n", serv_name, port);
			freeaddrinfo(serv_info);
			return -1;
		}
		
		rc = connect(fd, p->ai_addr, p->ai_addrlen);
		if(0 != rc)
		{
			perror("connect");
			exit(1);
		}
		

		freeaddrinfo(serv_info);
		fds[i] = fd;
		
		//~ sleep(1);
		chutil_make_non_blocking(fd);
		
		if(i == 2) msg_hdr.magic = SATOSHI_MAGIC_MAIN;
		memcpy(buf, &msg_hdr, sizeof(msg_hdr));
		
		
		
		write(fd, buf, sizeof(msg_hdr));
		
	}
	
	sleep(3);
	
	for(i = 0; i < ROUNDS; ++i)
	{
		if(fds[i] != -1)
		{
			shutdown(fds[i], 2);
			close(fds[i]);
		}
		fds[i] = -1;
	}
	quit = 1;
	return 0;
}
