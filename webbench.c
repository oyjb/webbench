/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include  <unistd.h> 
#include  <sys/param.h> 
#include  <rpc/types.h> 
#include  <getopt.h> 
#include  <strings.h> 
#include  <time.h> 
#include  <signal.h> 

/* values */
volatile int timerexpired = 0; //根据命令行参数-t指定的测试时间判断是否超时
int speed = 0; //成功得到服务器相应的子进程总数
int failed = 0; //子进程请求失败总数
int bytes = 0;  //读取到的字节数
/* globals */
int http10 = 1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */  //http版本定义
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method = METHOD_GET;  //定义HTTP请求方法GET 此外还支持OPTIONS、HEAD、TRACE方法，在main函数中用switch判断
int clients = 1;  //默认并发数为1，也就是子进程个数 可以由命令行参数-c指定
int force = 0;   //是否等待从服务器获取数据 0为等待
int force_reload = 0; //是否使用cache  0为使用
int proxyport = 80;  //代理服务器端口 80
char *proxyhost = NULL;  //代理服务器IP  默认为NULL
int benchtime = 30;  //测试时间  默认为30秒 可由命令行参数-t指定

/* internal */
int mypipe[2];  //创建管道（半双工）父子进程通信，读取/写入数据
char host[MAXHOSTNAMELEN]; //定义服务器主机名
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];  //http请求信息

//定义长选项
static const struct option long_options[] =
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
   timerexpired = 1;
}	

static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time  < sec >           Run benchmark for  < sec >  seconds. Default 30.\n"
	"  -p|--proxy  < server:port >  Use proxy server for request.\n"
	"  -c|--clients  < n >          Run  < n >  HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])
{
	int opt = 0;
	int options_index = 0;
	char *tmp = NULL;

	if(argc == 1)
	{
		usage();
		return 2;
	} 

	//使用getopt_long()函数从命令行选项得到输入请求
	//逐个处理选项
	while((opt = getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
	{
		switch(opt)
		{
			case  0 : break;
			case 'f': force = 1;break;
			case 'r': force_reload = 1;break; //是否使用cache，若命令行带r选项，则设置为使用cache
			case '9': http10 = 0;break;
			case '1': http10 = 1;break;
			case '2': http10 = 2;break;
			case 'V': printf(PROGRAM_VERSION"\n");exit(0); //输出本程序的版本号
			case 't': benchtime = atoi(optarg);break; //optarg表示测试时间，存储在全变量optarg中。说明：跟随冒号的带有附加选项的选项存储在optarg这个全局变量中
			case 'p': //正常的命令形式应该是 -p hostname:port  也就是代理服务器主机名(ip地址)对应端口号
					 /* proxy server parsing server:port */
					 //strrchr()函数返回查找字符在指定字符串中从正面开始的最后一次出现的位置
					 //与之对应的是strchr()，查找字符串s中首次出现字符c的位置
					 tmp = strrchr(optarg,':');
					 proxyhost = optarg;
					 if(tmp == NULL) //如果失败，则返回NULL（false）
					 {
						 break;
					 }
					 if(tmp == optarg) //如果开头就是冒号，则说明缺少hostname或者说是ip地址
					 {
						 fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
						 return 2;
					 }
					 if(tmp == optarg+strlen(optarg)-1) //最后一个字符是冒号，则说明冒号后面缺少端口号
					 {
						 fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
						 return 2;
					 }
					 *tmp='\0'; //输入正常的情况下，tmp指向optarg中间冒号的位置，在该位置切断(*tmp='\0')，前面是hostnam后面是端口号
					 proxyport = atoi(tmp+1);break; //往前进一位就是端口号位置, 把端口号字符串转换为整型 
				//输出帮助信息
			case ':': //遇到 冒号 ':' 和 字符'h' 以及 问号 '？'都输出帮助信息 
			case 'h':
			case '?': usage();return 2;break;
			   
			case 'c': clients = atoi(optarg);break; //选项c后面跟着的是并发进程数目，需要转化为整数存放于clients中
		}
	}
 
	//如果此时全局变量optind已经指向最后一个选项参数后面了，则说明缺少目标URL，则输出帮助信息
	if(optind == argc)
	{
		fprintf(stderr,"webbench: Missing URL!\n");
		usage();
		return 2;
	}
	//并发数为0，重置为1
	if(clients == 0)
		clients = 1;
	
	//测试时间为0，置为60秒
	if(benchtime == 0)
		benchtime = 60;

	/* Copyright */
	//#define PROGRAM_VERSION "1.5" ，所以在下面的printf中会直接把PROGRAM_VERSION替换成1.5
	fprintf(stderr,"Webbench - Simple Web Benchmark " PROGRAM_VERSION "\n""Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n");
	
	//此时optind指向URL的位置，执行函数build_request()函数，请求访问该网站
	build_request(argv[optind]);
	
	/* print bench info */
	printf("\nBenchmarking: ");
	switch(method)
	{
		 case METHOD_GET:
		 default:
			 printf("GET");break;
		 case METHOD_OPTIONS:
			 printf("OPTIONS");break;
		 case METHOD_HEAD:
			 printf("HEAD");break;
		 case METHOD_TRACE:
			 printf("TRACE");break;
	}
	printf(" %s",argv[optind]);
	switch(http10)
	{
		 case 0: printf(" (using HTTP/0.9)");break;
		 case 2: printf(" (using HTTP/1.1)");break;
	}
	printf("\n");
	if(clients == 1) printf("1 client");
	else
	   printf("%d clients",clients);

	printf(", running %d sec", benchtime);
	if(force) printf(", early socket close");
	if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
	if(force_reload) printf(", forcing reload");
	printf(".\n");
	return bench();
}

//处理请求，访问URL
//创建好的请求存放于全局变量request中
void build_request(const char *url)
{
	char tmp[10];
	int i;

	bzero(host,MAXHOSTNAMELEN); //host是服务器主机名
	bzero(request,REQUEST_SIZE); //request是请求信息

	//以下处理一些输入选项错误的情况，比如http10
	if(force_reload && proxyhost != NULL && http10 < 1)
		http10 = 1;
	if(method == METHOD_HEAD && http10 < 1)
		http10 = 1;
	if(method == METHOD_OPTIONS && http10 < 2)
		http10 = 2;
	if(method == METHOD_TRACE && http10 < 2)
		http10 = 2;

	//根据不同的
	switch(method)
	{
		default:
		case METHOD_GET:
			strcpy(request,"GET");break;
		case METHOD_HEAD:
			strcpy(request,"HEAD");break;
		case METHOD_OPTIONS:
			strcpy(request,"OPTIONS");break;
		case METHOD_TRACE:
			strcpy(request,"TRACE");break;
	}

	strcat(request," "); //连接一个空字符
	
	if(NULL == strstr(url,"://")) //判断URL是否合法
	{
		fprintf(stderr, "\n%s: is not a valid URL.\n",url);
		exit(2);
	}
	if(strlen(url) > 1500)
	{
		fprintf(stderr,"URL is too long.\n");
		exit(2);
	}
	//代理服务器是否为空
	if(proxyhost == NULL) 
	{
		if (0 != strncasecmp("http://",url,7))
		{
			fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
			exit(2);
		}
	}

	/* protocol/host delimiter */
	i = strstr(url,"://")-url+3; //想象一下 http://www.baidu.com/
	/* printf("%d\n",i); */      //它的要求是url必须要以http://开头，以/结尾

	if(strchr(url+i,'/') == NULL) //判断是否以/结尾
	{
		fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
		exit(2);
	}
	//代理服务器如果为空，那么就试图从url中提取域名信息和端口号等信息
	if(proxyhost == NULL)
	{
	   /* get port from hostname */
		if(index(url+i,':') != NULL && index(url+i,':') < index(url+i,'/'))
		{
			strncpy(host,url+i,strchr(url+i,':')-url-i);
			bzero(tmp,10); //把冒号后面的端口号拷贝到tmp中
			strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
			/* printf("tmp=%s\n",tmp); */
			proxyport = atoi(tmp);
			if(proxyport == 0) proxyport = 80;
		}
		else
		{
			strncpy(host,url+i,strcspn(url+i,"/"));
		}
		// printf("Host=%s\n",host);
		strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
	}
	//代理服务器非空，那么直接把url拼接到请求request当中
	else
	{
	   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
	   strcat(request,url);
	}
	if(http10 == 1)
		strcat(request," HTTP/1.0");
	else if (http10 == 2)
		strcat(request," HTTP/1.1");
	
	strcat(request,"\r\n");
	
	if(http10 > 0)
		strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
	if(proxyhost == NULL && http10 > 0)
	{
		strcat(request,"Host: ");
		strcat(request,host);
		strcat(request,"\r\n");
	}
	if(force_reload && proxyhost != NULL)
	{
		strcat(request,"Pragma: no-cache\r\n");
	}
	if(http10 > 1)
		strcat(request,"Connection: close\r\n");
	  /* add empty line at end */
	if(http10 > 0) 
		strcat(request,"\r\n"); 
	// printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
	int i, j, k;
	pid_t pid = 0;
	FILE *f;

	/* check avaibility of target server */
	i = Socket(proxyhost == NULL ? host : proxyhost, proxyport);
	if(i < 0)
	{
		fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
		return 1;
	}
	close(i);
	/* create pipe */ //创建管道
	if(pipe(mypipe))
	{
		perror("pipe failed.");
		return 3;
	}

	/* not needed, since we have alarm() in childrens */
	/* wait 4 next system clock tick */
	/*
	cas = time(NULL);
	while(time(NULL) == cas)
		sched_yield();
	*/

	/* fork childs */
	for(i = 0; i < clients; i++)
	{
		pid = fork(); //创建子进程
		if(pid  <=  (pid_t) 0) //只有子进程才会pid = 0 或者fork()出错会执行if体内代码
		{
			/* child process or error */
			sleep(1); /* make childs faster */ //让子进程先运行
			break; //break是为了跳出循环，否则子进程再继续执行循环就混乱了
		}
	}

	//创建子进程失败，即fork()失败的子进程
	if( pid <  (pid_t) 0)
	{
		fprintf(stderr, "problems forking worker no. %d\n",i);
		perror("fork failed.");
		return 3;
	}

/****************************************************************************************************/
	//子进程执行代码, 向管道写入数据
	if(pid ==  (pid_t) 0)
	{
		//I 'm a child
		if(proxyhost == NULL)
			benchcore(host,proxyport,request);
		else
			benchcore(proxyhost,proxyport,request);

		f = fdopen(mypipe[1], "w"); //子进程向描述符mypipe[1]写入结果
		if(f == NULL)
		{
			perror("open pipe for writing failed.");
			return 3;
		}
		// fprintf(stderr,"Child - %d %d\n",speed,failed);
		fprintf(f,"%d %d %d\n", speed, failed, bytes);
		fclose(f);
		return 0;
	}
/****************************************************************************************************/



/****************************************************************************************************/
	//父进程向管道读取数据
	else
	{
		//FILE * fdopen(int fildes,const char * mode);会将参数fildes 的文件描述词，转换为对应的文件指针后返回
		//fdopen通常作用于由创建管道和网络通信通道函数获得的描述符 来使一个标准I/O流与此文件描述符相关联
		f = fdopen(mypipe[0],"r"); 
		if(f == NULL) 
		{
			perror("open pipe for reading failed.");
			return 3;
		}
		setvbuf(f, NULL, _IONBF, 0); //setvbuf函数把一个流与缓冲区关联起来
		speed = 0;
		failed = 0;
		bytes = 0;
		while(1)
		{
			pid = fscanf(f, "%d %d %d", &i, &j, &k);
			if(pid < 2)
			{
				fprintf(stderr,"Some of our childrens died.\n");
				break;
			}
			speed += i;
			failed += j;
			bytes += k;
			/* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
			if(--clients == 0)
				break;
		}
		fclose(f);
		
		//读取完毕，输出总体速度等信息
		printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		(int)((speed+failed)/(benchtime/60.0f)),
		(int)(bytes/(float)benchtime),
		speed,
		failed);
	}
/****************************************************************************************************/

	return i;
}


/******************************
这里才是测试http的地方
@ host:地址
@ port：端口
@ req：http格式方法
********************************/
void benchcore(const char *host, const int port, const char *req)
{
	int rlen;
	char buf[1500];
	int s, i;
	struct sigaction sa;

	/* setup alarm signal handler */
	sa.sa_handler = alarm_handler;
	sa.sa_flags = 0;
	if( sigaction(SIGALRM, &sa, NULL) )  //sigaction函数查询或设置信号处理方式
		exit(3);
	alarm(benchtime);

	rlen = strlen(req);

nexttry:
	while(1)
	{
		if(timerexpired)
		{
			if(failed > 0)
			{
				/* fprintf(stderr,"Correcting failed by signal\n"); */
				failed--;
			}
			return;
		}
		s = Socket(host,port);                          
		if(s < 0)
		{
			failed++;continue;
		} 

		if(rlen!=write(s,req,rlen))
		{
			failed++;
			close(s);
			continue;
		}

		if(http10 == 0)
		{
			if(shutdown(s,1))
			{
				failed++;
				close(s);
				continue;
			}
		}

		if(force == 0)
		{
			/* read all available data from socket */
			while(1)
			{
				if(timerexpired)
					break; 
				i = read(s,buf,1500);
				/* fprintf(stderr,"%d\n",i); */
				if(i < 0) 
				{
					failed++;
					close(s);
					goto nexttry;
				}
				else
				{
				   if(i == 0)
					   break;
				   else
					   bytes+=i;
				}
			}
		}
		if(close(s))
		{
			failed++;
			continue;
		}
		++speed;
	}
}
