# test1

统计在某个算力比例下投票时，当1000个区块中的投票量达到目标阈值时，所需的最少出块数量

编译：$ gcc -O2 -Wall -o statistics statistics.c  -lm -lpthread
	
用法：	
$ ./statistics [预期值] [目标值] > [outputfile]

示例： 
$ ./statistics 700 750 result.txt




