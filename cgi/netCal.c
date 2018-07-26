#include<stdio.h>
#include<strings.h>
#include<stdlib.h>
#include<string.h> 

#define MAX 1024

void mycal(char* buf)
{
	int x,y;
	sscanf(buf,"firstdata=%d&lastdata=%d",&x,&y);
	
	printf("<html>\n");
	printf("<body>\n");

	printf("<h3>%d + %d = %d</h3>\n",x,y,x+y);
	printf("<h3>%d - %d = %d</h3>\n",x,y,x-y);
	printf("<h3>%d * %d = %d</h3>\n",x,y,x*y);
	if(y==0)
	{
		printf("<h3>%d / %d = %d</h3>,%s\n",x,y,-1,"(zero)");
		printf("<h3>%d % %d = %d</h3>,%s\n",x,y,-1,"(zero)");
	}
	else
	{
		printf("<h3>%d / %d = %d</h3>\n",x,y,x/y);
		printf("<h3>%d %% %d = %d</h3>\n",x,y,x%y);
	}
	printf("</body>\n");
	printf("</html>\n");
}


int main()
{
	//获得有效参数
	char buf[MAX]={0};
	if(getenv("METHOD"))
	{
		if(strcasecmp(getenv("METHOD"),"GET")==0)
		{
			strcpy(buf,getenv("QUERY_STRING"));
		}
		else
		{
			int content_length=atoi(getenv("CONTENT_LENGTH"));
			int i=0;
			char c;
			for(;i<content_length;i++)
			{
				read(0,&c,1);
				buf[i]=c;
			}
			buf[i]='\0';
		}
	}
	mycal(buf);

	return 0;
}
