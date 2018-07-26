#include<stdio.h>
#include<stdlib.h> 
#include<strings.h> 
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h> 
#include<netinet/in.h>
#include<arpa/inet.h>
#include<pthread.h> 
#include<fcntl.h> 
#include<string.h>
#include<signal.h> 

#define MAX 1024
#define HOME_PAGE "index.html"

static void usage(const char* proc)
{
	printf("Usage:%s port\n",proc);
}

int startup(int port)
{
	int sock=socket(AF_INET,SOCK_STREAM,0);
	if(sock<0)
	{
		perror("socket");
		exit(2);
	}
	
	//保证服务器主动断开连接的时候，不能让服务器因为TIME_WAIT问题而不能重启获取新的连接
	int opt=1;
	setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

	struct sockaddr_in local;
	local.sin_family=AF_INET;
	local.sin_addr.s_addr=htonl(INADDR_ANY);
	local.sin_port=htons(port);
	
	if(bind(sock,(struct sockaddr*)&local,sizeof(local))<0)
	{
		perror("bind");
		exit(3);
	}

	if(listen(sock,5)<0)
	{
		perror("listen");
		exit(4);
	}

	return sock;

}

int get_line(int sock,char line[],int size)
{
	int c='a';
	int i=0;
	ssize_t s=0;
	while(i<size-1 && c!='\n')
	{
		s=recv(sock,&c,1,0);
		if(s>0)
		{
			//如果读到的字符是'\r'，则需要对下一个字符进行窥探，只是看下一个字符是啥，并不会取出来
			if(c=='\r')
			{
				recv(sock,&c, 1,MSG_PEEK);
				//如果'\r'下一格字符不是'\n'则表示一行结束，统一将最后的换行符置为'\n'
				if(c!='\n')
				{
					c='\n';
				}
				//如果是'\n'就将其取出
				else
				{
					recv(sock,&c,1,0);
				}
			}
			//如果读到的字符是'\n'或其他就直接将其存进缓冲区中
			line[i++]=c;
		}
		else
		{
			break;
		}
	}
	//给一行字符后面加上结束符'\0'
	line[i]='\0';
	//返回一行字符的个数
	return i;
}

void clear_header(int sock)
{
	char line[MAX];
	do
	{
		get_line(sock,line,sizeof(line));
	}while(strcmp(line,"\n")!=0);
}

//非cgi方式，只是简单的网页显示
void echo_www(int sock,char* path,int size,int* err)
{
	//从sock里读数据一直读到空行停下来,解决TCP粘包问题
	clear_header(sock);

	//sendfile()函数
	int fd=open(path,O_RDONLY);
	if(fd<0)
	{
		*err=404;
		return;
	}
	
	//响应
	char line[MAX];//定义响应报头
	memset(line, 0x00, sizeof line);
	sprintf(line,"HTTP/1.0 200 OK\r\n");
	//发送状态行
	send(sock,line,strlen(line),0);
	sprintf(line,"Content-Type:text/html;charset=ISO-8859-1\r\n");
	send(sock,line,strlen(line),0);
	sprintf(line,"\r\n");
	send(sock,line,strlen(line),0);
	
	//将path写到sock中
	
	sendfile(sock,fd,NULL,size);

	close(fd);
}

void echo_error(int code)
{
	switch(code)
	{
		case 404:
			break;
		case 501:
			break;
		default:
			break;
	}
}

int exe_cgi(int sock,char path[],char method[],char* query_string)
{
	char line[MAX];
	int content_length=-1;

	//环境变量的定义
	char method_env[MAX/32];
	char query_string_env[MAX];
	char content_length_env[MAX/16];

	//获取资源参数阶段
	if(strcasecmp(method,"GET")==0)
	{
		//GET方法的参数已经拿到，不需要报头，将报头清理掉
		clear_header(sock);
		//GET方法的参数在query_string中进行保存是已知的
	}
	//POST
	//需要先进行读取请求报头获取Content-Length的大小，由于POST方法的资源参数是在请求正文，Content-Length表示的是正文的大小，防止粘包问题
	else
	{
		do
		{
			get_line(sock,line,sizeof(line));
			//需要精准匹配
			if(strncmp(line,"Content-Length: ",16)==0)
			{
				content_length=atoi(line+16);
			}
		}while(strcmp(line,"\n")!=0);
		//说明有效载荷(资源参数)不存在，直接返回
		if(content_length==-1)
		{
			return 404;
		}
	}
	
	printf("method:%s,path:%s\n",method,path);

	//响应部分
	sprintf(line,"HTTP/1.0 200 OK\r\n");
	send(sock,line,strlen(line),0);
	sprintf(line,"Content-Type:text/html;charset=ISO-8859-1\r\n");
	send(sock,line,strlen(line),0);
	sprintf(line,"\r\n");
	send(sock,line,strlen(line),0);

	//线程是进程的执行分支，线程执行fork()相当于http服务器进程执行fork()
	//让子进程去执行path所指向的cgi
	
	//进程之间是相互独立的，要想子进程从父进程获取参数，父进程获取到子进程的执行结果，必须要进行进程间通信
	//利用管道，创建两个管道，父进程将参数传给子进程，子进程将结果传给父进程
	int input[2];
	int output[2];
	
	pid_t id=fork();
	if(id<0)
	{
		return 404;//服务器内部出错
	}
	else if(id==0)
	{
		//子进程:需要从父进程获取方法和参数，如果直接将method,query_string,content_length,请求正文都写入管道将没有办法进行区分
		//程序替换时不会替换掉环境变量，可以将方法和参数以环境变量的方式传给子进程
		
		//文件描述符重定向
		//从input去读，output去写，关闭input[1],output[0]
		close (input[1]);
		close (output[0]);
		//进程程序的替换,不会替换文件描述符表，进行重定向会更加方便
		dup2(input[0],0);
		dup2(output[1],1);

		//导入,获得环境变量
		sprintf(method_env,"METHOD=%s",method);
		putenv(method_env);
		if(strcasecmp(method,"GET")==0)
		{
			sprintf(query_string_env,"QUERY_STRING=%s",query_string);
			putenv(query_string_env);
		}
		else
		{
			sprintf(content_length_env,"CONTENT_LENGTH=%d",content_length);
			putenv(content_length_env);
		}

		//程序进行替换
		//第一个参数path表示程序的路径
		//第二个参数path表示在命令行如何去执行
		execl(path,path,NULL);
		exit(1);
	}
	else
	{
		//父进程(这里的父进程是http服务器程序的一个子线程，等待其子进程退出，不会影响主程序的执行)
		close(input[0]);
		close(output[1]);

		//读取请求正文
		char c;
		if(strcasecmp(method,"POST")==0)
		{
			int i=0;
			for(;i<content_length;i++)
			{
				read(sock,&c,1);
				write(input[1],&c,1);
			}
		}

		//GET和POST都要做，从子进程中获取结果，并发送给浏览器
		while(read(output[0],&c,1)>0)
		{
			send(sock,&c,1,0);
		}


		waitpid(id,NULL,0);
		
		//关闭对应的文件描述符
		close(input[1]);
		close(output[0]);
	}
	return 200;
}

static void *handler_request(void *arg)
{
	int sock=(int)arg;
	char line[MAX] = {};
	char method[MAX/32] = {};//保存方法
	char url[MAX] = {};//保存url
	char path[MAX] = {};//资源路径 wwwroot
	int errCode=200;
	int cgi=0;//通用网关接口，http内部的功能，处理GET和POST方法
	char* query_string=NULL;//存放GET方法的参数

#ifdef Debug
	//打印信息，用于调试
	do
	{
		get_line(sock,line,sizeof(line));
		printf("%s",line);

	}while(strcmp(line,"\n")!=0);//当读到空行的时候说明请求报头读完闭
#else
	//处理逻辑
	//第一行读失败，说明http不能处理
	if(get_line(sock,line,sizeof(line))<0)
	{
		errCode=404;
		goto end;
	}
	
	//line[]=GET / HTTP/1.1
	//提取method
	int i=0;
	int j=0;
	while(i<sizeof(method)-1 && j<sizeof(line) && !isspace(line[j]))
	{ 
		method[i]=line[j];
		i++;j++;
	}
	method[i]='\0';
	
	//由于此服务器只能处理GET和POST方法，需要对获取的method进行判断
	//有可能GET和POST是某个字母小写，利用函数strcasecmp()进行比较，为真则表示不相等
	//只有是POST或者是带参的GET方法才可以使用cgi模式
	if(strcasecmp(method,"GET")==0)
	{
		//后面需要判断是否带参，将参数提取出来，所以这里不进行处理
	}
	else if(strcasecmp(method,"POST")==0)
	{
		cgi=1;	
	}
	else
	{
		errCode=404;
		goto end;
	}

	//这时j指向空格，处理j后面可能会有好多空格的情况
	while(j<sizeof(line) && isspace(line[j]))
	{
		j++;
	}

	//这时j指向非空格,提取url
	i=0;
	while(i<sizeof(url)-1 && j<sizeof(line) && !isspace(line[j]))
	{
		url[i]=line[j];
		i++;j++;
	}
	url[i]='\0';

	//GET和POST方法的区别：GET方法的参数拼接在url后面用?进行隔开，POST方法的参数在http正文
	//如果url后面没有参数，http正文也没有参数，则默认是GET方法
	
	//从刚才提取的url中获取GET方法的参数query_string(?前面赋给url，后面赋给query_string)
	if(strcasecmp(method,"GET")==0)
	{
		query_string=url;
		while(*query_string)
		{
			if(*query_string=='?')
			{
				//将？变为'\0'字符串变为两部分，url是一部分，query_string为一部分
				*query_string='\0';
				query_string++;
				//GET方法带参数就要以cgi方式运行
				cgi=1;
				break;
			}
			query_string++;
		}
		//query_string为空
	}

	//method[GET|POST] url[] query_string[NULL|arg] cgi(0|1)
	//如url -> /a/b/c.html 要给其前面拼接上wwwroot
	//sprintf()函数
	sprintf(path,"wwwroot%s",url);//wwwroot后面不用带/，url本来就有
	//处理url中只存在/的情况，直接将首页HOME_PAGE拼接上去
	//strlen(path)为path的长度减1表示最后一个元素，如果最后一个元素是/则需要拼接首页
	if(path[strlen(path)-1]=='/')
	{
		strcat(path,HOME_PAGE);
	}
	
	//stat()函数成功返回0，判断path路径是否存在
	struct stat st;
	if(stat(path,&st)<0)
	{
		errCode=404;
		goto end;
	}
	else
	{
		//如果文件时候一个目录，将首页拼上
		if(S_ISDIR(st.st_mode))
		{
			strcat(path,HOME_PAGE);
		}
		else
		{
			//当文件存在并具有可执行权限时就以cgi方式运行，所以先判断是否具有可执行权限
			if((st.st_mode & S_IXUSR)||(st.st_mode & S_IXGRP)||(st.st_mode & S_IXOTH))
			{
				cgi=1;
			}
		}
		if(cgi)
		{
			//除了显示网页情况下，其他使用cgi的情况都是只读了一行数据
			errCode=exe_cgi(sock,path,method,query_string);
		}
		else
		{
			//不用cgi方式运行就将资源路径返回即可
			echo_www(sock,path,st.st_size,&errCode);
		}
	}
	
	

#endif

	//将sock用完之后调用close关闭
	//回收了本地文件描述符资源，关闭了连接
end:
	if(errCode!=200)
	{
		echo_error(errCode);
	}
	close(sock);
}

//http 8080
int main(int argc,char* argv[])
{
	if(argc!=2)
	{
		usage(argv[0]);
		return 1;
	}

	//创建监听套接字
	int listen_sock=startup(atoi(argv[1]));
	//防止http刚发起请求，浏览器关掉了；操作系统干掉这个进程
	signal(SIGPIPE,SIG_IGN);

	//事件处理
	for(;;)
	{
		struct sockaddr_in client;
		socklen_t len=sizeof(client);
		int new_sock=accept(listen_sock,(struct sockaddr*)&client,&len);
		if(new_sock<0)
		{
			perror("accept");
			continue;
		}

		//创建线程
		pthread_t id;
		//创建子线程处理所指定的获取的连接new_sock
		pthread_create(&id,NULL,handler_request,(void*)new_sock);
		//将子线程进行分离，方便父线程继续获取连接
		pthread_detach(id);
	}
}
















