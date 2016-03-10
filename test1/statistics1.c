/*
 * statistics.c
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
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

#define WINDOW_SIZE (1000)

#define TEST_ROUNDS 100000
#define DEAD_LINE (144 * 180)
static uint64_t found_at[TEST_ROUNDS];

typedef struct stat_window
{
	unsigned char version[WINDOW_SIZE];
	int pos;
	int count;
	int support;
	int threshold; 
}stat_window_t;

static volatile int quit;

static void init(stat_window_t * st, int support, int threshold)
{
	memset(st, 0, sizeof(stat_window_t));
	st->support = support;
	st->threshold = threshold;
}

static uint64_t mining(stat_window_t * st, uint64_t start_pos)
{	
	uint64_t i = start_pos;
	unsigned short support;
	while(!quit)
	{
		if(i == UINT64_MAX) 
		{
			fprintf(stderr, "integer overflow\n");
			break;
		}
		
		st->count -= st->version[st->pos];
		support = rand() % 1000;		
		
		st->version[st->pos] = (support < st->support)?1:0;
		st->count += st->version[st->pos++];		
		if(st->pos == WINDOW_SIZE) st->pos = 0;
		
		if(st->count >= st->threshold) break;
		
		if(i > DEAD_LINE)
		{
			return -1;
		}
		
		++i;
		if((i % 1000) == 0)
		{
			// display info
			//~ printf("%"PRIu64":\t count = %d\n", i, st->count); 
		}
	}
	return i;
}

static void * worker_thread(void * param)
{
	stat_window_t * st = (stat_window_t *)param;
	if(NULL == st)
	{
		fprintf(stderr, "invalid parameter\n");
		return (void *)1;
	}
	
	int i;
	uint64_t found;
	uint64_t min = UINT64_MAX, max = 0, total = 0, average = 0;
	
	for(i = 0; (i < TEST_ROUNDS) && !quit; ++i)
	{
		memset(st->version, 0, sizeof(st->version));
		st->pos = 0;
		st->count = 0;
		
		found = mining(st, 0);
		if(found != -1)
		{			
			if(found <= min) min = found;
			if(found >= max) max = found;
			printf("found at pos: %ld\n", found);
			total += found;
		}
		
		
		found_at[i] = found;
		
		
	}
	
	average = total / i;
	
	double percent = 0;
	uint64_t deadline_days = DEAD_LINE; // 180天
	int count = 0;
	for(i = 0; i < TEST_ROUNDS; ++i)
	{
		if((found_at[i] != -1) && (found_at[i] <= deadline_days)) ++count;
	}
	
	percent = (double)count / (double)TEST_ROUNDS;
	
	
	
	
	printf("===================\n"	
			"support = %d, threshold = %d\n"
			"------------------\n"
			"average:%ld\nmin: %ld\nmax: %ld\n"
			"the possibility of found within 180 days: %.4f%%\n"
			"===================\n",
			st->support, st->threshold,
			average, min, max,
			percent * 100.0);
	
	quit = 1;
	fprintf(stderr, "Press enter to quit.\n");
	pthread_exit((void *)0);
}


int main(int argc, char **argv)
{
	srand(time(NULL));
	stat_window_t st;
	int support = 700;
	int threshold = 750;
	
	if(argc > 1) support = atol(argv[1]);
	if(argc > 2) threshold = atol(argv[2]);
	
	init(&st, support, threshold);
	
	pthread_t th;
	void * ret_code = NULL;
	int rc;
	rc = pthread_create(&th, NULL, worker_thread, &st);
	
	if(0 != rc)
	{
		perror("pthread_create");
		exit(1);
	}
	
	char c;
	while(!quit)
	{
		rc = scanf("%[^\n]%*c", &c);
		if((EOF == c) || ('q' == c) || ('Q' == c)) 
		{
			break;
		}
	}
	quit = 1;
	pthread_join(th, &ret_code);
	

	
	
	return 0;
}

