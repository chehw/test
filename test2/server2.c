/*
 * server2.c
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
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <search.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#define _STAND_ALONE
#define _DEBUG

#define PEER_BUFFER_SIZE (1 << 16) // 64k
#define MAX_EVENTS 64
#define MAX_PAYLOAD_SIZE (1 << 28) // 256 MB

#ifdef _STAND_ALONE

#ifdef _DEBUG
#define debug_printf(fmt, ...)  do { \
		char buf[512] = ""; \
		int cb = 0; \
		cb = snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
		write(STDERR_FILENO, buf, cb); \
	}while(0)
#define log_printf(fmt, ...) do { \
		char buf[512] = ""; \
		int cb = 0; \
		cb = snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
		write(STDOUT_FILENO, buf, cb); \
	}while(0)
#else
#define debug_printf(fmt, ...) 
#define log_printf(fmt, ...)
#endif

typedef void (SIGPROC)(int, siginfo_t *, void *);

static sigset_t sig_masks;

#define SATOSHI_MAGIC_MAIN 		(0xD9B4BEF9)
#define SATOSHI_MAGIC_TESTNET3 	(0x0709110B)


#endif // _STAND_ALONE

static uint32_t SATOSHI_MAGIC = SATOSHI_MAGIC_MAIN;

static volatile int quit;
static char SERV_NAME[100] = "127.0.0.1";
static char PORT[NI_MAXSERV] = "43690"; 
static int LEVEL = 0;
static int VERBOSE = 0;
static char DATA_PATH[200] = "./data";
static volatile int conns = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct satoshi_msg_version
{
	int32_t version; // protocol version
	uint64_t services;
	uint64_t timestamp; 
	//
	//
	uint64_t nonce;
}satoshi_msg_version_t;

typedef struct satoshi_msg_header
{
	uint32_t magic;
	char command[12];
	uint32_t length;
	uint32_t checksum;
}satoshi_msg_header_t;

typedef struct satoshi_peer_info
{
	int fd;
	pthread_mutex_t write_mutex;
	satoshi_msg_version_t * msg_ver;
	uint64_t stime; // start time
	uint64_t ltime; // last access time
	uint64_t bytes_recv;
	uint64_t bytes_send;
	satoshi_msg_header_t msg_hdr;
	unsigned char * payload;
	size_t cur_pos;
	
	char rbuf[512]; // recv buffer
	size_t cb_rbuf;
	
	char sbuf[PEER_BUFFER_SIZE]; // send buffer
	size_t cb_sbuf;
}satoshi_peer_info_t;

static satoshi_peer_info_t * satoshi_peer_info_new(int fd)
{
	satoshi_peer_info_t * peer = (satoshi_peer_info_t *)calloc(sizeof(satoshi_peer_info_t), 1);
	assert(NULL != peer);
	peer->fd = fd;
	peer->stime = time(NULL);
	peer->ltime = peer->stime;
	peer->cur_pos = -1;
	pthread_mutex_init(&peer->write_mutex, NULL);
	debug_printf("alloc peer_info: 0x%p.\n", peer);
	return peer;
}

static void satoshi_peer_info_destroy(satoshi_peer_info_t * peer)
{
	if(NULL == peer) return;
	if(-1 != peer->fd) 
	{
		close(peer->fd);
		peer->fd = -1;
	}
	if(NULL != peer->payload)
	{
		free(peer->payload);
		peer->payload = NULL;
	}
	if(NULL != peer->msg_ver)
	{
		free(peer->msg_ver);
		peer->msg_ver = NULL;
	}
	pthread_mutex_destroy(&peer->write_mutex);
	
	debug_printf("free peer_info: 0x%p.\n", peer);
	free(peer);
}



static void register_sig_handlers(SIGPROC _sig_handler_cb);
static void sig_handler(int sig, siginfo_t * si, void * user_data);

// global functions
//~ static void do_cleanup();
//~ static void server_run();
static int parse_args(int * p_argc, char *** p_argv);


static int server_run();
static void poll_stdin();
static int chutil_make_non_blocking(int fd);

static void * server_thread(void * user_data)
{
	int rc;
	rc = server_run();
	if(!quit) quit = 1;
	pthread_exit((void *)((long)rc));
}

//*********************************************************
//** main
//** 
int main(int argc, char **argv)
{
	int rc;
	
	register_sig_handlers(sig_handler);
	rc = parse_args(&argc, &argv);
	if(0 != rc) return rc;
	
	log_printf("STATUS: host = %s, port = %s, data_path = %s, level = %d, verbose = %d\n",
		SERV_NAME, PORT, DATA_PATH, LEVEL, VERBOSE);
		
	
	pthread_t th[1];
	void * ret_code = NULL;
	rc = pthread_create(th, NULL, server_thread, NULL);
	if(0 != rc)
	{
		perror("pthread_create");
		return -1;
	}
	poll_stdin();
	pthread_join(th[0], &ret_code);
	
	pthread_mutex_destroy(&g_mutex);
	return (int)(long)ret_code;
}

static void on_error(int efd, struct epoll_event * p_event)
{
	int rc;
	assert(NULL != p_event);
	
	debug_printf("on_error: fd = %d\n", p_event->data.fd);
	
	if(-1 == p_event->data.fd) return;
	
	satoshi_peer_info_t * peer = (satoshi_peer_info_t *)p_event->data.ptr;
	if(NULL != peer && (-1 != p_event->data.fd))
	{
		int fd = peer->fd;
		struct epoll_event event;
		event.data.ptr = peer;
		event.events = 0;
		rc = epoll_ctl(efd, EPOLL_CTL_DEL, fd, &event);
		if(0 != rc)
		{
			perror("epoll_ctl DEL");			
		}
		
		close(peer->fd);
		free(peer);
	}
}

static void on_close(int efd, struct epoll_event * p_event)
{
	debug_printf("on_close: fd = %d\n", p_event->data.fd);
	
}


static int on_accept(int efd, struct epoll_event * p_event)
{
	int err_code;
	int rc;
	assert(NULL != p_event);
	int sfd = p_event->data.fd;
	
	debug_printf("on_accept: sfd = %d\n", sfd);
	
	while(1)
	{
		err_code = 0;
		struct sockaddr_storage ss;
		socklen_t len;
		int fd;
		char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
		len = sizeof(ss);
		fd = accept(sfd, (struct sockaddr *)&ss, &len);
		
		
		if(-1 == fd)
		{
			if((EAGAIN == errno) || (EWOULDBLOCK == errno))
			{
				break;
			}else
			{
				err_code = errno;
				perror("accept");
				break;
			}
		}
		// display peer info
		rc = getnameinfo((struct sockaddr *)&ss, len, hbuf, sizeof(hbuf), 
			sbuf, sizeof(sbuf),
			NI_NUMERICHOST | NI_NUMERICSERV);
		if(0 == rc)
		{
			log_printf("Accepted connection on [%d], (%s:%s)\n",
				fd, hbuf, sbuf);
		}
		rc = chutil_make_non_blocking(fd);
		if(0 != rc)
		{
			debug_printf("ERROR: chutil_make_non_blocking failed.\n");
			close(fd);
			err_code = -1;
			break;
		}
		
		satoshi_peer_info_t * peer = satoshi_peer_info_new(fd);
		assert(NULL != peer);
		
		struct epoll_event event = {0};
		event.data.ptr = peer;
		event.events = EPOLLIN | EPOLLET;
		rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);
		if(0 != rc)
		{
			perror("epoll_ctl ADD");
			err_code = rc;
			break;
		}
		
	}
	
	return err_code;
}

static int msg_handler(satoshi_peer_info_t * peer)
{
	debug_printf("command = %12s, payload length = %u\n", 
		peer->msg_hdr.command, peer->msg_hdr.length);
	if(NULL != peer->payload)
	{
		free(peer->payload);
		peer->payload = NULL;
		peer->cur_pos = -1;
		memset(&peer->msg_hdr, 0, sizeof(satoshi_msg_header_t));
	}
	return 0;
}

static int on_read(int efd, struct epoll_event * p_event)
{
	// EPOLLET	
	assert(NULL != p_event);
	satoshi_peer_info_t * peer = (satoshi_peer_info_t *)p_event->data.ptr;
	
	if(-1 == p_event->data.fd || NULL == p_event->data.ptr) return -1;
	
	int done = 0;
	int fd = peer->fd;
	debug_printf("on_read: fd = %d\n", fd);
	
	
	while(1)
	{
		char * buf = &peer->rbuf[peer->cb_rbuf];	
		ssize_t cb;
		cb = read(fd, buf, sizeof(peer->rbuf) - peer->cb_rbuf);
		if(-1 == cb)
		{
			if(EAGAIN != errno) // socket error
			{
				perror("read");
				done = 1;
			}
			break;
		}else if(0 == cb)
		{
			// remote closed the connection
			done = 1;
			break;
		}
		peer->cb_rbuf += cb;
		
		if(peer->cur_pos == -1) // msg_header has not been pasred
		{
			debug_printf("read msg_hdr: cb = %d\n", (int)cb);
			
			if(peer->cb_rbuf < sizeof(satoshi_msg_header_t)) // partial of satoshi_msg_header
			{
				continue;
			}
			peer->cur_pos = 0;
			
			// copy [msg_header] from the buffer			
			satoshi_msg_header_t * msg_hdr = (satoshi_msg_header_t *)peer->rbuf;
			memcpy(&peer->msg_hdr, peer->rbuf, sizeof(satoshi_msg_header_t));
			
			if(peer->msg_hdr.magic != SATOSHI_MAGIC)
			{
				debug_printf("ERROR: invalid magic number (0x%.8x).\n", peer->msg_hdr.magic);
				done = 1;
				break;
			}else
			{
				debug_printf("magic = (0x%.8x), command = %s\n", peer->msg_hdr.magic, peer->msg_hdr.command);
			}
			
			// read payload	
			if(peer->msg_hdr.length) // has payload
			{
				if(peer->msg_hdr.length > MAX_PAYLOAD_SIZE)
				{
					debug_printf("ERROR: insufficient memory.\n");
					done = 1;
					break;
				}
				
				peer->payload = (unsigned char *)malloc(peer->msg_hdr.length);
				if(NULL == peer->payload)
				{
					perror("malloc");					
					done = 1;
					break;
				}
				
				unsigned char * p = (unsigned char *)peer->rbuf + sizeof(satoshi_msg_header_t);
				unsigned char * p_end = (unsigned char *)peer->rbuf + peer->cb_rbuf;
				assert((p <= p_end));
				
				size_t cb_available = p_end - p;				
				cb = (cb_available > msg_hdr->length) ? msg_hdr->length : cb_available;				
				
				memcpy(peer->payload, p, cb);
				
				peer->cur_pos = cb;
				
				if(cb < cb_available) 
				{
					peer->cb_rbuf = cb_available - cb;
					memmove(peer->rbuf, &p[cb], peer->cb_rbuf);
				}else
				{
					memset(peer->rbuf, 0, sizeof(peer->rbuf));
					peer->cb_rbuf = 0;
				}
				
				if(peer->cur_pos == peer->msg_hdr.length)
				{
					msg_handler(peer);
				}
			}else // no payload
			{
				msg_handler(peer);
			}
		}else
		{
			if(peer->cur_pos < peer->msg_hdr.length)
			{
				size_t cb_left = peer->msg_hdr.length - peer->cur_pos;
				if(cb_left > cb) cb_left = cb;
				
				memcpy(peer->payload + peer->cur_pos, buf, cb_left);
				peer->cur_pos += cb_left;
				
				if(cb_left < cb)
				{
					peer->cb_rbuf = cb - cb_left;
					memmove(peer->rbuf, &buf[cb_left], peer->cb_rbuf);					
				}else
				{
					memset(peer->rbuf, 0, sizeof(peer->rbuf));
					peer->cb_rbuf = 0;
				}
			}
			if(peer->cur_pos == peer->msg_hdr.length)
			{
				msg_handler(peer);
			}
		}
		
	}
	
	if(done)
	{
		// do cleanup
		printf("Closed connection on [fd = %d]\n", fd);
		shutdown(fd, 2);
		if(NULL != peer)
		{
			satoshi_peer_info_destroy(peer);
		}
	}
	
	return 0;
}


int server_run()
{
	int rc;
	int sfd;
	struct addrinfo hints, * serv_info, *p;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	rc = getaddrinfo(SERV_NAME, PORT, &hints, &serv_info);
	if(0 != rc)
	{
		debug_printf("ERROR: getaddrinfo: %s\n", gai_strerror(rc));
		return -1;
	}
	for(p = serv_info; NULL != p; p = p->ai_next)
	{
		sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(-1 == sfd) continue;
		rc = bind(sfd, p->ai_addr, p->ai_addrlen);
		if(0 == rc) break;
		close(sfd);
	}
	
	if(NULL == p)
	{
		debug_printf("Could not bind to [%s:%s]\n", SERV_NAME, PORT);
		freeaddrinfo(serv_info);
		return -1;
	}
	freeaddrinfo(serv_info);
	
	chutil_make_non_blocking(sfd);
	rc = listen(sfd, SOMAXCONN);
	if(0 != rc)
	{
		perror("listen");
		close(sfd);
		return -1;
	}
	// waiting for incomming connections
	int efd;
	struct epoll_event event, events[MAX_EVENTS];
	memset(&event, 0, sizeof(event));
	memset(events, 0, sizeof(events));
	
	efd = epoll_create1(0);
	if(-1 == efd)
	{
		perror("epoll_create1");
		close(sfd);
		return -1;
	}
	
	event.data.fd = sfd;
	event.events = EPOLLIN | EPOLLET;
	
	rc = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
	if(0 != rc)
	{
		perror("epoll_ctl");
		close(sfd);
		close(efd);
		return -1;
	}
	
	static const int timeout = 1000; // 1000 ms
	while(!quit)
	{
		int n, i;
		n = epoll_pwait(efd, events, MAX_EVENTS, timeout, &sig_masks);
		if(n < 0)
		{
			perror("epoll_pwait");
			rc = -1;
			break;
		}
		else if(n == 0) // timeout
		{
			continue;
		}
		for(i = 0; i < n; ++i)
		{
			if( (events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) )
			{
				// error_handler
				if(events[i].data.fd == sfd)
				{
					debug_printf("ERROR: epoll wait error on [fd = %d].\n", sfd);
					close(sfd);
				}else
				{				
					on_error(efd, &events[i]);
				}
				continue;
			}else if( events[i].events & EPOLLRDHUP)
			{
				if(events[i].data.fd == sfd)
				{
					debug_printf("ERROR: epoll wait error on [fd = %d].\n", sfd);
					close(sfd);
				}else
				{
					on_close(efd, &events[i]);
				}
				continue;
			}
			else if(events[i].events & EPOLLIN)
			{
				if(events[i].data.fd == sfd) // incomming connections
				{
					rc = on_accept(efd, &events[i]);
					if(0 != rc)
					{
						debug_printf("ERROR: on_accept\n");
					}				
					continue;
				}else // data came from peers
				{
					rc = on_read(efd, &events[i]);
					if(0 != rc)
					{
						debug_printf("ERROR: on_read\n");
					}
					continue;
				}
			}
		}
	}
	
	
	close(efd);
	
	shutdown(sfd, 2);
	close(sfd);
	return rc;
}

//**************************************************
//** parse_args: 
//**     use getopt_long to parse commandline args
//** 
static int parse_args(int * p_argc, char *** p_argv)
{
	int argc = *p_argc;
	char ** argv = * p_argv;
	int c;
	int digit_optind = 0;
	static struct option long_options[] = {
		{"host", required_argument, NULL, 's'},
		{"port", required_argument, NULL, 'p'},
		{"testnet", no_argument, NULL, 't'},
		{"verbose", no_argument, NULL, 'v'},
		{"data_path", required_argument, NULL, 'd'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};
	
	while(1)
	{
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		c = getopt_long(argc, argv, ":s:pvh012t", long_options, &option_index);
		if(-1 == c) break;
		
		switch(c)
		{
		case 0:
			printf("option %s", long_options[option_index].name);
			if(optarg)
			{
				printf("with arg %s\n", optarg);
			}else printf("\n");
			break;
		case '0': case '1': case '2':
			if(digit_optind != 0 && digit_optind != this_option_optind)
			{
				fprintf(stderr, "invalid digits args\n");
				return -1;
			}
			digit_optind = this_option_optind;
			LEVEL = c - '0';
			printf("option %c\n", c);
			break;
		case 't':
			SATOSHI_MAGIC = SATOSHI_MAGIC_TESTNET3;
			break;
		case 'h':
			printf("USUAGE: %s "
						"[--host=hostname] [--port=port] [--data_path=data_path] "
						"[--verbose] [--help] [-012]\n", 
						argv[0]);
			return 1;
		case 's':
			strncpy(SERV_NAME, optarg, sizeof(SERV_NAME));
			break;
		case 'p':
			strncpy(PORT, optarg, sizeof(PORT));
			break;
		case 'd':
			strncpy(DATA_PATH, optarg, sizeof(DATA_PATH));
			break;
		case 'v':
			VERBOSE = 1;
			break;
		
		default:
			fprintf(stderr, "?? getopt returned char code 0%o ??\n", c);
			return -1;
		}
		
	}
	
	return 0;
}


#ifdef _STAND_ALONE


static void sig_handler(int sig, siginfo_t * si, void * user_data)
{	
	switch(sig)
	{
	case SIGINT: case SIGHUP: case SIGUSR1:
		quit = 1;
		break;
	case SIGUSR2:
		// do sth.
		break;
	default:
		break;
	}
#ifdef _DEBUG
	fprintf(stderr, "sig = %d\n", sig);	
#endif
}

static void register_sig_handlers(SIGPROC _sig_handler_cb)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = _sig_handler_cb;
	
	if(sigaction(SIGINT, &sa, NULL) == -1)
	{
		perror("sigaction SIGINT");
		exit(1);	
	}
	if(sigaction(SIGHUP, &sa, NULL) == -1)
	{
		perror("sigaction SIGHUP");		
		exit(1);
	}
	if(sigaction(SIGUSR1, &sa, NULL) == -1)
	{
		perror("sigaction SIGUSR1");		
		exit(1);
	}
	if(sigaction(SIGUSR2, &sa, NULL) == -1)
	{
		perror("sigaction SIGUSR2");		
		exit(1);
	}
	
	sigemptyset(&sig_masks);
	sigaddset(&sig_masks, SIGINT);
	sigaddset(&sig_masks, SIGHUP);
	sigaddset(&sig_masks, SIGUSR1);
	sigaddset(&sig_masks, SIGUSR2);
}




static void poll_stdin()
{
	// ppoll stdin with sig_masks
	int n;
	struct pollfd pfd[1];
	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN | POLLPRI;
	
	struct timespec timeout = {0, 500000000}; // 500 ms
	
	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	while(!quit)
	{
		int i;
		n = ppoll(pfd, 1, &timeout, &sig_masks);
		if(n < 0)
		{
			perror("poll");
			break;
		}
		if(0 == n) continue;
		for(i = 0; i < n; ++i)
		{
			if(pfd[i].fd == STDIN_FILENO)
			{
				if(pfd[i].revents & POLLIN)
				{
					char * line = NULL;
					size_t len = 0;
					ssize_t cb;
					cb = getline(&line, &len, stdin);
					if(cb > 0)
					{
						if(line[cb - 1] == '\n') line[--cb] = '\0';						
						if( (strcasecmp(line, "quit") == 0) ||
							(strcasecmp(line, "exit") == 0) )
						{
							quit = 1;
						}
					}
					free(line);
					if(quit) goto label_exit;
				}
			}
		}
	}
	
label_exit:	
	if(!quit) quit = 1;
}


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

#endif // _STAND_ALONE

#undef PEER_BUFFER_SIZE
#undef MAX_EVENTS
