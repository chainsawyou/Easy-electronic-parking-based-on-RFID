#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <termios.h> //配置串口的头文件
#include <sys/mman.h>
#include <time.h>

#define FIFO_NAME  "/rfidll/image/mplayer.fifo"


#define PATH1 "/rfidll/image/1.bmp"//入口界面 
#define PATH2 "/rfidll/image/2.bmp"//退出界面 
#define PATH3 "/rfidll/image/3.bmp"//注册成功界面 
#define PATH4 "/rfidll/image/4.bmp" //管理员登录成功页图 
#define PATH5 "/rfidll/image/5.bmp" //停车成功界面
#define PATH6 "/rfidll/image/6.bmp" //结费驶离界面 
#define PATH7 "/rfidll/image/7.bmp" //头像1 
#define PATH8 "/rfidll/image/8.bmp" //头像2 
#define PATH9 "/rfidll/image/9.bmp" //头像3 
#define PATH10 "/rfidll/image/10.bmp" //头像选择界面	 

unsigned int card_ID = 0;
unsigned int id = 0;
unsigned char recvbuf0[15];
int *plcd;//内存映射区指针
int lcd_fd;//fb0设备文件描述符
unsigned char wbuf[32];
unsigned char rbuf[32];
char tmp1[100]; //离开时间 
char tmp2[100]; //停车时间
int Cid = 0;

//卡序号,姓名、电话号码、车牌号、卡号、余额、进入时间，退出时间 
struct Person 
{
	int Cid;
	char name[12];
	char tel[11];
	char license_num[10];
	unsigned int card_id;
	int cost;
	time_t starttime,endtime;//进入退出时间
	int parking_flag;//停车标记，若是0则代表未停车，若是1则代表已停车 
	int avatar; 
};

struct Person person_buf[12];
struct Person current_person;
int count = 0;

/******
串口初始化
******/	 
int uart_init(const char * uart_name,int baudrate)
{
	int uart1_fd= open(uart_name, O_RDWR);
	if (uart1_fd == -1)
	{
		perror("open error:");
		return -1;
	}
	struct termios myserial;
	memset(&myserial, 0, sizeof (myserial));
	//O_RDWR               
	myserial.c_cflag |= (CLOCAL | CREAD);
	myserial.c_cflag &= ~CSIZE;   
	myserial.c_cflag &= ~CRTSCTS; 
	myserial.c_cflag |= CS8;      
	myserial.c_cflag &= ~CSTOPB; 
	myserial.c_cflag &= ~PARENB;  

	if(baudrate == 9600)
	{
		cfsetospeed(&myserial, B9600);  
		cfsetispeed(&myserial, B9600);
	}
	else if(baudrate == 115200)
	{
		cfsetospeed(&myserial, B115200);  
		cfsetispeed(&myserial, B115200);
	}
	
	tcflush(uart1_fd, TCIFLUSH);

	tcsetattr(uart1_fd, TCSANOW, &myserial);
	return   uart1_fd;
}

/********
初始化lcd屏幕
********/ 
void lcd_init()
{
	lcd_fd = open("/dev/fb0",O_RDWR);
	if(-1 == lcd_fd)
	{
		printf("open lcd error!\n");
		//return -1;
	}
	plcd = mmap(NULL,800*480*4,PROT_READ|PROT_WRITE,MAP_SHARED,lcd_fd,0);
	if(MAP_FAILED == plcd)  
	{
		printf("map lcd error!\n");
		//return -1;
	}
}

/*********
绘图 
*********/ 
void lcd_draw_point(int x,int y,int color1)
{
	*(plcd+y*800+x) = color1;
}

void show_word(unsigned char word[],int width,int length,int x0,int y0,int color1)
{
	int i,j,k;
	int x; 
	for(i = 0;i < length;i++)
	{
		x = word[i];
		for(k = 7;k >= 0;k--)
		{
			if(x & (0x01 << k))
			{
				lcd_draw_point(x0 + (7-k) + (i%(width/8)*8),y0 + (i/(width/8)),color1);
			}
		}
	}
}

/*********
绘制BMP
*********/ 
void lcd_draw_BMP(int x,int y,const char *bmpname)
{
	unsigned char buf[4];
	int fd = open(bmpname,O_RDONLY);
	int i,j;
	
	lseek(fd,0,SEEK_SET);
	read(fd,buf,2);
	if(buf[0] != 0x42 || buf[1] != 0x4D)
	{
		printf("this picture is not bmp!");
		return;
	}
	
	int bmp_w = 0;
	lseek(fd,0x12,SEEK_SET);
	read(fd,&bmp_w,4);
	
	int bmp_h = 0;
	lseek(fd,0x16,SEEK_SET);
	read(fd,&bmp_h,4);
	
	int bmp_colordepth = 0;
	lseek(fd,0x1C,SEEK_SET);
	read(fd,&bmp_colordepth,2);
	
	//printf("\nbmp_w:%ld\nbmp_h:%ld\nbmp_colordepth:%ld\n",bmp_w,bmp_h,bmp_colordepth);
	
	lseek(fd,54,SEEK_SET);
	for(i = 0;i < bmp_h;i++)
	{
		for(j = 0;j < bmp_w;j++)
		{		
			int color = 0;
			read(fd,&color,bmp_colordepth/8);
			lcd_draw_point(x+j,y+(bmp_h>0?(bmp_h-1-i):i),color);
		}
		lseek(fd,(4-bmp_colordepth/8*bmp_w%4)%4,SEEK_CUR);
	}
	close(fd);
}

/******
关闭lcd屏幕
******/ 
void lcd_uninit()
{
	close(lcd_fd);
	munmap(plcd,800*480*4);
}

/*******
读卡 
*******/

unsigned char CalBCC(unsigned char *buf,int n)
{
	int i;
	unsigned char bcc = 0;
	for(i = 0;i < n;i++)
	{bcc ^= *(buf+i);}
	return (~bcc);
}

int PiccRequest(int fd) //请求天线范围的卡 
{
	int ret = 0;
	wbuf[0] = 0x07;          //帧长    
	wbuf[1] = 0x02;			 //包号 = 0，命令类型 = 0x02
	wbuf[2] = 'A';			 //命令'A' 
	wbuf[3] = 0x01;			 //数据长度
	wbuf[4] = 0x52;			 //请求模式
	wbuf[5] = CalBCC(wbuf,wbuf[0]-2);	 //校验和
	wbuf[6] = 0x03;			 //结束帧
		write(fd,wbuf,7);   //请求过程代码  发送命令
		usleep(50000); //延时等待
		ret = read(fd,rbuf,8); //接收回复数据
		if(ret ==8)
		{
			return rbuf[2]; //返回状态值，若为0，表示请求成功
		}
		else
		{
			//printf("read error,ret:%d\n",ret);
			return -1;
		}
}

int PiccAntioll(int fd) //防碰撞代码 ，获取天线范围内ID最大的卡 
{
	int ret;
	wbuf[0] = 0x08;          //帧长    
	wbuf[1] = 0x02;			 //包号 = 0，命令类型 = 0x01
	wbuf[2] = 'B';			 //命令'B' 
	wbuf[3] = 0x02; 			 //数据长度
	wbuf[4] = 0x93;			 //请求模式
	wbuf[5] = 0x00;
	wbuf[6] = CalBCC(wbuf,wbuf[0]-2);	 //校验和
	wbuf[7] = 0x03;			 //结束帧
	write(fd,wbuf,8);
	usleep(50000);
	ret = read(fd,rbuf,10);
	
	if(rbuf[2] == 0x00)
	{
		card_ID = (rbuf[4])|(rbuf[5]<<8)|(rbuf[6]<<16)|(rbuf[7]<<24);
		//printf("the card is %x\n",card_ID);
		return 0;
	}
	
}


/*******
注册模式
*******/ 
void register_mode(int fd)
{	
    int i = 0;
	printf("*****register_mode*****\n");
	printf("Input your name:\n");
	scanf("%s",person_buf[count].name);
	printf("Input your tel-number:\n");
	scanf("%s",person_buf[count].tel);
	printf("Input your car-License-number:\n");
	scanf("%s",person_buf[count].license_num);
	printf("Amount of deposit:\n");
	scanf("%d",&person_buf[count].cost);
	printf("Choose your avatar follow 3 styles\nInuput 1 (comic) 2 (reality) 3 (scene):\n");
	lcd_draw_BMP(0,0,PATH10);
	scanf("%d",&person_buf[count].avatar);
	switch(person_buf[count].avatar)
	{
		case 1:
			lcd_draw_BMP(0,0,PATH7);
			break;
		case 2:
			lcd_draw_BMP(0,0,PATH8);
			break;
		case 3:
			lcd_draw_BMP(0,0,PATH9);
			break;
	}
	printf("brush your card:\n");
	while(1)
	{
			if(PiccRequest(fd))//请求天线范围的卡
				{
					//printf("1 The Request Failed!\n");
					continue;
				}
			
			if(PiccAntioll(fd))//进行防碰撞，获取天线范围内最大的ID
				{
					//printf("2 RFID PiccAnticoll Fialed!\n");
					break;
				}
			//printf("card_ID is %x \n",card_ID);
	}
	
	person_buf[count].card_id = card_ID;
	printf("Saved your CardID is:%u\n",person_buf[count].card_id);	
	person_buf[count].Cid = count;
	printf("Register sucessful!\nyour card Cid is:%d\n you saved %d\n",person_buf[count].Cid,person_buf[count].cost);
	lcd_draw_BMP(0,0,PATH3);//显示注册成功界面 
	//printf("Register-Information:\nname：%s\ncar-License-number:%s\nCardID：%u\nCid is saved：%d\n",person_buf[count].name,person_buf[count].license_num,person_buf[count].card_id,person_buf[count].Cid);
	count++;
}

/********
登录模式
********/
int login_mode(int fd)
{
	printf("\n"); 
	printf("*****login-mode******\n");
	printf("Input your Cid(card number):"); 
	scanf("%d",&Cid);
	switch(person_buf[Cid].avatar)
	{
		case 1:
			lcd_draw_BMP(0,0,PATH7);
			break;
		case 2:
			lcd_draw_BMP(0,0,PATH8);
			break;
		case 3:
			lcd_draw_BMP(0,0,PATH9);
			break;
	}
	printf("Bursh Card!********\n"); 
	int i = 0;
	while(1)
	{
	if(PiccRequest(fd))//请求天线范围的卡
				{
					//printf("1 The Request Failed!\n");
					continue;
				}
			
			if(PiccAntioll(fd))//进行防碰撞，获取天线范围内最大的ID
				{
					//printf("2 RFID PiccAnticoll Fialed!\n");
					break;
				}
	}
	if(person_buf[Cid].card_id = card_ID)
	{
	printf("Your Wallet is %d dollars\n",person_buf[Cid].cost);
	printf("login-sucessful!\n");
	}
	else
	{
		printf("login-failed\n");
	}
} 
/******
管理员模式
******/ 
void administor_mode(int fd)
{
	printf("\n");
	char isQ = 'E';
	printf("Welcome!\n");
	printf("User-information:(registered %d )enter Q to quit\n",count);
	while(isQ != 'Q')
	{
		printf("Input Cid to check User-information:\n");
		scanf("%d",&Cid);
		printf("the user is:%s\n",person_buf[Cid].name);
		printf("CardID is:%u\n",person_buf[Cid].card_id);
		printf("Quit?enter Q/C to quit/continue\n");
		scanf("%s",&isQ);
	} 
}
/******
停车模式 
******/ 
void parting_mode(int fd)
{
	/*//函数原型：size_t strftime (char* ptr, size_t maxsize, const char* format, const struct tm* timeptr);
			strftime(tmp2,sizeof(tmp2),"%Y-%m-%d  %H:%M:%S",localtime(&car_buf[i].starttime));
			*/ 
	printf("\n");
	printf("*******parking-mode********\n");
	printf("Bursh Card!********\n"); 
	do
	{
	while(1)
	{
	if(PiccRequest(fd))//请求天线范围的卡
				{
					//printf("1 The Request Failed!\n");
					continue;
				}
			
			if(PiccAntioll(fd))//进行防碰撞，获取天线范围内最大的ID
				{
					//printf("2 RFID PiccAnticoll Fialed!\n");
					break;
				}
	}
	}while(person_buf[Cid].card_id = card_ID);
	printf("You are in the parking lot ready to park!\n");
	lcd_draw_BMP(0,0,PATH5);//显示停车页面 
	//Debug
	printf("your Cid is %d:\n",person_buf[Cid].Cid);
	//Debug
	person_buf[Cid].starttime = time(NULL);
	printf("Parting is successful!\nBilling has begun.\n");
	person_buf[Cid].parking_flag = 1;
	strftime(tmp2,sizeof(tmp2),"%Y-%m-%d  %H:%M:%S",localtime(&person_buf[Cid].starttime));
	printf("\n");
	printf("Parting time is%s\n",tmp2);
	//Debug
	printf("Your Wallet is %d dollars\n",person_buf[Cid].cost);
	//Debug
}
/******
离开结费 
*****/ 

void SavedMoney()
{
	int money = 0;
	printf("Please Enter the amount you want to deposit: ");	
	scanf("%d",&money);
	person_buf[Cid].cost += money;
	printf("\n");
	printf("The deposit is successful and your current balance is: %d",person_buf[Cid].cost);
} 
void leave_mode(int fd)
{
	//card_ID = 0;
	printf("\n");
	int i,j = 0;
	printf("*****Leave the parking lot*****\n");
	printf("brush your card:\n");
	int money;
	while(1)
	{
	if(PiccRequest(fd))//请求天线范围的卡
				{
					//printf("1 The Request Failed!\n");
					continue;
				}
			
			if(PiccAntioll(fd))//进行防碰撞，获取天线范围内最大的ID
				{
					//printf("2 RFID PiccAnticoll Fialed!\n");
					break;
				}
	}
	printf("\n*************\n");
	if(person_buf[Cid].parking_flag == 1 && person_buf[Cid].card_id == card_ID)
	{
			person_buf[Cid].parking_flag == 0;
			person_buf[Cid].endtime=time(NULL);
			strftime(tmp1,sizeof(tmp1),"%Y-%m-%d  %H:%M:%S",localtime(&person_buf[Cid].endtime));
			printf("departure time:%s\n",tmp1);
			printf("Total parking time:%ds\n",person_buf[Cid].endtime-person_buf[Cid].starttime); 
			money=((person_buf[Cid].endtime-person_buf[Cid].starttime)/60)*1;
			printf("Your Wallet is %d dollars\n",person_buf[Cid].cost);
			if(person_buf[Cid].cost < 0)
			{
				printf("You are overdue, please pay in time!\n");
				SavedMoney();
			}
			person_buf[Cid].cost -= money;
			lcd_draw_BMP(0,0,PATH6);  
			printf("co-consumption: %d(It's $1 per minute)\nbalance:%d\n",money,person_buf[Cid].cost);
			return;
	
	}
	else
	{
		printf("Quit failed!!!!");
		leave_mode(fd);
	}
}

int PlayMP3()
{
	int fd,r;

	pid_t pid = fork();
	if(pid > 0)
	{
		r = mkfifo(FIFO_NAME,0664);
		if(r == -1)
		{
			perror("mkfifo failed");
			return -1;
		}

		fd = open(FIFO_NAME, O_WRONLY);
		if(fd == -1)
		{
			perror("open fifo failed");
			return -1;
		}

		printf("father\n");
		while(1)
		{
			printf("father\n");
			char cmd[64];
			fgets(cmd,64,stdin);
			puts(cmd);
			if(strcmp(cmd,"quit\n")==0)
			{
				break;
			}
			write(fd,cmd,strlen(cmd));//鍚戠閬撳啓鍛戒护
		}

	
		char cmd[64];
		sprintf(cmd,"kill -9 %d",pid);
		system(cmd);
		
		close(fd);

		unlink(FIFO_NAME);//鍒犻櫎绠￠亾鏂囦欢
		puts("bye bye~");
	}
	else if(pid == 0)
	{
		printf("sun\n");
		int r = execlp("mplayer", "mplayer","-slave", "-quiet", "-input",  "file=/rfidll/image/mplayer.fifo",
		"-geometry",  "0:0", "-zoom", "-x", "400", "-y", "300",  "mplayer.mp3","&",NULL);

		if(r < 0)
		{
			perror("execlp error");
		}
	}
}

int main()
{
	
	int uart1_fd = uart_init("/dev/ttySAC1",9600);
	
	lcd_init();
	int x,y;
	int i;
	int mode1 = -1;
	int mode2 = -2;
	int flag = 0;
	int is_login = 10;
	
	while(1)
	{
		if(flag == 1)
		{
			lcd_draw_BMP(0,0,PATH2);
			printf("\nWelcome to your next stop!\n"); 
			PlayMP3();
			break;
		}
		
		if(is_login == 10)
		{
		lcd_draw_BMP(0,0,PATH1);//显示入口界面 
		//printf("Functions opts:\n");
		while(1)
		{
			printf("\nFunctions opts:\n");
			printf("0:exit\n1:register\n2:login\n3:administrator\n");
			scanf("%d",&mode1);
			switch(mode1)
		{
			case 0:
			printf("quit！\n\n");
			lcd_draw_BMP(0,0,PATH2);//显示退出界面 
			flag = 1;
				break;
				
			case 1:
			register_mode(uart1_fd);//注册
			is_login = 10;
				break;
				
			case 2:
			login_mode(uart1_fd);//登录 
			printf("You are logged in!\n");
			is_login = 20;
				break;
				
			case 3:
			lcd_draw_BMP(0,0,PATH4);
			administor_mode(uart1_fd);
			is_login = 10;
			break; 
		}	
			if(is_login == 20) break;
			if(flag == 1)
			{
				break;
			}
		}
		}
		if(is_login == 20 && flag !=1)
		{
		if(is_login == 10) break;
		while(flag != 1 && is_login == 20)
		{
		printf("\nPlease select Func:\n");
		printf("0:exit\n1:parking\n2:leave\n");
		scanf("%d",&mode2);
		switch(mode2)
		{
			case 0:
			printf("return to previous step!\n\n");
			is_login = 10; 
				break;
			
			case 1:
			parting_mode(uart1_fd);
				break;
			
			case 2:
			lcd_draw_BMP(0,0,PATH6);//显示驶离界面 
			leave_mode(uart1_fd);
			flag = 1;
				break; 
		}	
		} 
		}
	}
		
	sleep(3);
	close(uart1_fd);
	lcd_uninit();
}


