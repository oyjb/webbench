/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
/**********************************************************
 *@ 功能：通过地址和端口建立网络连接
 *@ host：网络地址
 *@ clientPort：端口号
 *@ return：建立的socket连接
 *@ 如果返回-1,表示建立连接失败
 *********************************************************/
 //本函数用于得到一个连接上服务器的socket描述符
 //若成功则返回该套接字描述符
int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad; //sockaddr_in 是包含地址族sin_family和端口号sin_port的结构体,其他的可以默认
    struct hostent *hp; //hostent是主机的完整信息
    
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET; // [tcp/ipv4]地址族
	
	//inet_addr(host)把点分制十进制的IP地址转换为32位二进制网络字节序的IPV4地址
	//如果参数有效则转换为对应的二进制IPV4地址，若无效，则为INADDR_NONE，所以要判断一下
    inaddr = inet_addr(host); //将点分的十进制的IP转为无符号长整形 
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
	//转换不成功，则把host当做是一个主机名通过函数gethostbyname获取主机信息，然后得到地址
    else
    {
        hp = gethostbyname(host);
        if (hp == NULL)
            return -1;
		//hp是一个struct hostent结构体，内含：#define h_addr h_addr_list[0]
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
	//htons()函数把端口号转成网络字节序的短整型
    ad.sin_port = htons(clientPort);
    
	//创建socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return sock;
	创建的sock如果没有绑定一个地址，则系统自动为其绑定一个地址，然后去连接host对应的服务器
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
	//一旦成功建立连接，sock就唯一标识了这个链接，客户端就可以通过读写这个sock来与服务端通信
    return sock;
}

