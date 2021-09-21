﻿
//整个程序入口函数放这里

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h> 
#include <errno.h>
#include <arpa/inet.h>

#include "ngx_macro.h"        
#include "ngx_func.h"          
#include "ngx_c_conf.h"        
#include "ngx_c_socket.h"      
#include "ngx_c_memory.h"      
#include "ngx_c_threadpool.h"  
#include "ngx_c_crc32.h"       
#include "ngx_c_slogic.h"      

//本文件用的函数声明
static void freeresource();

//和设置标题有关的全局量
size_t  g_argvneedmem=0;        
size_t  g_envneedmem=0;        
int     g_os_argc;             
char    **g_os_argv;            
char    *gp_envmem=NULL;        
int     g_daemonized=0;        

//socket/线程池相关
//CSocekt      g_socket;          
CLogicSocket   g_socket;       
CThreadPool    g_threadpool;   

//和进程本身有关的全局量
pid_t   ngx_pid;           
pid_t   ngx_parent;          
int     ngx_process;            
int     g_stopEvent;           

sig_atomic_t  ngx_reap;                                       

//程序主入口函数----------------------------------
int main(int argc, char *const *argv)
{   
    //printf("%u,%u,%u",EPOLLERR ,EPOLLHUP,EPOLLRDHUP);  
    //exit(0);

    int exitcode = 0;           //退出代码，先给0表示正常退出
    int i;                      //临时用
    
    //(0)先初始化的变量
    g_stopEvent = 0;            //标记程序是否退出，0不退出          

    //(1)无伤大雅也不需要释放的放最上边    
    ngx_pid    = getpid();      //取得进程pid
    ngx_parent = getppid();     //取得父进程的id 
    //统计argv所占的内存
    g_argvneedmem = 0;
    for(i = 0; i < argc; i++)  //argv =  ./nginx -a -b -c asdfas
    {
        g_argvneedmem += strlen(argv[i]) + 1; //+1是给\0留空间。
    } 
    //统计环境变量所占的内存。注意判断方法是environ[i]是否为空作为环境变量结束标记
    for(i = 0; environ[i]; i++) 
    {
        g_envneedmem += strlen(environ[i]) + 1; 
    } //end for

    g_os_argc = argc;           //保存参数个数
    g_os_argv = (char **) argv; //保存参数指针

    //全局量有必要初始化的
    ngx_log.fd = -1;                  
    ngx_process = NGX_PROCESS_MASTER; 
    ngx_reap = 0;                     /
   
    //(2)初始化失败，就要直接退出的
    CConfig *p_config = CConfig::GetInstance(); //单例类
    if(p_config->Load("nginx.conf") == false) //把配置文件内容载入到内存            
    {   
        ngx_log_init();    //初始化日志
        ngx_log_stderr(0,"配置文件[%s]载入失败，退出!","nginx.conf");

        exitcode = 2; //标记找不到文件
        goto lblexit;
    }
    //(2.1)内存单例类可以在这里初始化
    CMemory::GetInstance();	
    //(2.2)crc32校验算法单例类可以在这里初始化
    CCRC32::GetInstance();
        
    //(3)一些必须事先准备好的资源，先初始化
    ngx_log_init();                
        
    //(4)一些初始化函数，准备放这里        
    if(ngx_init_signals() != 0) //信号初始化
    {
        exitcode = 1;
        goto lblexit;
    }        
    if(g_socket.Initialize() == false)//初始化socket
    {
        exitcode = 1;
        goto lblexit;
    }

    //(5)一些不好归类的其他类别的代码，准备放这里
    ngx_init_setproctitle();    

    //------------------------------------
    //(6)创建守护进程
    if(p_config->GetIntDefault("Daemon",0) == 1) /
    {
        //1：按守护进程方式运行
        int cdaemonresult = ngx_daemon();
        if(cdaemonresult == -1) //fork()失败
        {
            exitcode = 1;    //标记失败
            goto lblexit;
        }
        if(cdaemonresult == 1)
        {
            //这是原始的父进程
            freeresource();   
                             
            exitcode = 0;
            return exitcode;  //整个进程直接在这里退出
        }

        g_daemonized = 1;   
    }

    //(7)开始正式的主工作流程，主流程一致在下边这个函数里循环，暂时不会走下来，资源释放啥的日后再慢慢完善和考虑    
    ngx_master_process_cycle(); 
        
    //--------------------------------------------------------------    
    //for(;;)    
    //{
    //    sleep(1); //休息1秒        
    //    printf("休息1秒\n");        
    //}
      
    //--------------------------------------
lblexit:
    //(5)该释放的资源要释放掉
    ngx_log_stderr(0,"程序退出，再见了!");
    freeresource();  //一系列的main返回前的释放动作函数
    //printf("程序退出，再见!\n");    
    return exitcode;
}


void freeresource()
{
    //(1)对于因为设置可执行程序标题导致的环境变量分配的内存，我们应该释放
    if(gp_envmem)
    {
        delete []gp_envmem;
        gp_envmem = NULL;
    }

    //(2)关闭日志文件
    if(ngx_log.fd != STDERR_FILENO && ngx_log.fd != -1)  
    {        
        close(ngx_log.fd); 
        ngx_log.fd = -1;        
    }
}
