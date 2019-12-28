#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include "threadpool.h"

#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404
#define TIMELOG   444
#define CHANGELINE 555
#define BEGIN 111

#define SEM_NAME "sem_example"
#define SHM_NAME "mmap_example"

#ifndef SIGCLD
#   define SIGCLD SIGCHLD
#endif

char timecal[BUFSIZE];
double time_readsocket;
double time_writesocket;
double time_readweb;
double time_writelog;

struct {
    char *ext;
    char *filetype;
} extensions [] = {
    {"gif", "image/gif" },
    {"jpg", "image/jpg" },
    {"jpeg","image/jpeg"},
    {"png", "image/png" },
    {"ico", "image/ico" },
    {"zip", "image/zip" },
    {"gz",  "image/gz"  },
    {"tar", "image/tar" },
    {"htm", "text/html" },
    {"html","text/html" },
    {0,0}
};

typedef struct webparam
{
    int hit;
    int fd;
    int file_fd;
    char *buffer;
    threadpool* pool_rdmsg;
    threadpool* pool_rdfile;
    threadpool* pool_sdmsg;
}webparam;

#include "divfunc.h"


unsigned long get_file_size(const char *path)
{
    unsigned long filesize = -1;
    struct stat statbuff;
    if (stat(path, &statbuff) < 0)
    {
        return filesize;
    }
    else
    {
        filesize = statbuff.st_size;
    }
    return filesize;
}

void timegetter(char *timebuffer)
{
    time_t timep;
    struct tm *p;
    time (&timep);
    p = gmtime(&timep);
    (void)sprintf(timebuffer,"Date: %d.%d.%d\nTime: %d:%d:%d\n",1900+p->tm_year,1+p->tm_mon,p->tm_mday,8+p->tm_hour,p->tm_min,p->tm_sec);
}

void logger(int type, char *s1, char *s2, int socket_fd)
{
    int fd ;
    char timebuffer[BUFSIZE];
    char logbuffer[BUFSIZE*2];

    switch (type) {
        case ERROR:
            (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid());
            break;
        case FORBIDDEN:
            (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
            (void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
            break;
        case NOTFOUND:
            (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
            (void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
            break;
        case LOG:
            (void)sprintf(logbuffer,"INFO: %s:%s:%d",s1, s2,socket_fd);
            break;
        case TIMELOG:
            (void)sprintf(logbuffer,"The cost of %s is %s ms",s1, s2);
            break;
        case CHANGELINE:
            (void)sprintf(logbuffer,"\n");
            break;
        case BEGIN:
            timegetter(timebuffer);
            break;
        }
        /* No checks here, nothing can be done with a failure anyway */
        if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
            if (type == BEGIN)
                (void)write(fd,timebuffer,strlen(timebuffer));
            else
                (void)write(fd,logbuffer,strlen(logbuffer));
            (void)write(fd,"\n",1);
            (void)close(fd);
        }
        // if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
    }

/* this is a child web server process, so we can exit on errors */
void web(void *data)
{
    int fd, hit;
    struct timeval t1, t2;
    double timeslide;
    int j, file_fd, buflen;
    long i, ret, len;
    char * fstr;
    char buffer[BUFSIZE+1]; /* can't be static */
    memset(buffer, 0, BUFSIZE+1);
    webparam *param = (webparam*)data;
    fd = param->fd;
    hit = param->hit;
    // FILE *timefile;
    // char *timefilename = "time_file.txt";
    double time_temp_readsocket = 0;
    double time_temp_wirtesocket = 0;
    double time_temp_readweb = 0;
    double time_temp_writelog = 0;
    
    logger(BEGIN,"","",fd);

    gettimeofday(&t1, NULL);
    ret = read(fd,buffer,BUFSIZE);   /* read Web request in one go */
    gettimeofday(&t2, NULL);
    timeslide = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
    time_temp_readsocket = timeslide;
    // timefile = fopen(timefilename, "a");
    // fprintf(timefile, "the cost of read socket is : %f\n", timeslide);
    // fclose(timefile);

    //-------------------------------------------------------------------------------------------------------------------------------------------------------------------

    if(ret == 0 || ret == -1) {  /* read failure stop now */
        logger(FORBIDDEN,"failed to read browser request","",fd);
    }else{
        if(ret > 0 && ret < BUFSIZE)  /* return code is valid chars */
            buffer[ret]=0;    /* terminate the buffer */
        else buffer[0]=0;
        for(i=0;i<ret;i++)  /* remove CF and LF characters */
            if(buffer[i] == '\r' || buffer[i] == '\n')
                buffer[i]='*';
        logger(LOG,"request",buffer,hit);
        
        if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
            logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
        }
        for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
            if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
                buffer[i] = 0;
                break;
            }
        }
        for(j=0;j<i-1;j++)   /* check for illegal parent directory use .. */
            if(buffer[j] == '.' && buffer[j+1] == '.') {
                logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
            }
        if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
            (void)strcpy(buffer,"GET /index.html");

        /* work out the file type and check we support it */
        buflen = strlen(buffer);
        fstr = (char *)0;
        for(i=0;extensions[i].ext != 0;i++) {
            len = strlen(extensions[i].ext);
            if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
                fstr = extensions[i].filetype;
                break;
            }
        }
        if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

        if((file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
            logger(NOTFOUND, "failed to open file",&buffer[5],fd);
        }

        //---------------------------------------------------------------------------------------------------------------------------------------------------------------
        
        logger(LOG,"SEND",&buffer[5],hit);
        len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
            (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
            (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
        // it have to be two '\n' !!!

        logger(LOG,"Header",buffer,hit);

        gettimeofday(&t1, NULL);
        (void)write(fd,buffer,strlen(buffer));
        gettimeofday(&t2, NULL);
        timeslide = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
        time_temp_wirtesocket = timeslide;
        // timefile = fopen(timefilename, "a");
        // fprintf(timefile, "the cost of wirte socket is : %f\n", timeslide);
        // fclose(timefile);

        /* send file in 8KB block - last block may be smaller */
        while (1)
        {
            gettimeofday(&t1, NULL);
            ret = read(file_fd, buffer, BUFSIZE);
            gettimeofday(&t2, NULL);
            timeslide = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
            time_temp_readweb += timeslide;
            // timefile = fopen(timefilename, "a");
            // fprintf(timefile, "the cost of read webfile is : %f\n", timeslide);
            // fclose(timefile);
            if (ret > 0)
            {
                gettimeofday(&t1, NULL);
                (void)write(fd,buffer,ret);
                gettimeofday(&t2, NULL);
                timeslide = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
                time_temp_wirtesocket += timeslide;
                // timefile = fopen(timefilename, "a");
                // fprintf(timefile, "the cost of wirte socket is : %f\n", timeslide);
                // fclose(timefile);
            }
            else
            {
                break;
            }
        }
        
        // while ((ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
        //     (void)write(fd,buffer,ret);
        // }

        logger(CHANGELINE,"","",0);

        time_readsocket += time_temp_readsocket;
        time_writesocket += time_temp_wirtesocket;
        time_readweb += time_temp_readweb;
        time_writelog += time_temp_writelog;

        // sleep(1);  /* allow socket to drain before signalling the socket is closed */
        usleep(10000);  /* allow socket to drain before signalling the socket is closed */
        close(file_fd);
    }
    close(fd);
    free(param);
    // exit(1);/* with it will byte count wrong */
}

void* perform(void* data)
{
    int max1 = 0;
    int max2 = 0;
    int max3 = 0;
    int min1 = 100;
    int min2 = 100;
    int min3 = 100;
    webparam *param = (webparam*)data;
    while (1)
    {
        usleep(100000); /* 100ms */
        if (max1 < param->pool_rdmsg->num_working)
        {
            max1 = param->pool_rdmsg->num_working;
        }
        if (max2 < param->pool_rdfile->num_working)
        {
            max2 = param->pool_rdfile->num_working;
        }
        if (max3 < param->pool_sdmsg->num_working)
        {
            max3 = param->pool_sdmsg->num_working;
        }
        if (min1 > param->pool_rdmsg->num_working)
        {
            min1 = param->pool_rdmsg->num_working;
        }
        if (min2 > param->pool_rdfile->num_working)
        {
            min2 = param->pool_rdfile->num_working;
        }
        if (min3 > param->pool_sdmsg->num_working)
        {
            min3 = param->pool_sdmsg->num_working;
        }
        FILE* perffile = fopen("maxmin.txt","a");
        fprintf(perffile,"%d %d %d %d %d %d %d %d %d\n",max1,max2,max3,min1,min2,min3
        ,param->pool_rdmsg->num_working,param->pool_rdfile->num_working,param->pool_sdmsg->num_working);
        fclose(perffile);
    }
}

int main(int argc, char **argv)
{
    struct timeval t1, t2;
    double timeslide;
    gettimeofday(&t1, NULL);
    int i, port, listenfd, socketfd, hit;
    socklen_t length;
    // FILE *timefile;
    // char *timefilename = "timefile.txt";
    // pid_t pid;

    static struct sockaddr_in cli_addr; /* static = initialised to zeros */
    static struct sockaddr_in serv_addr; /* static = initialised to zeros */

    time_readsocket = 0;
    time_writesocket = 0;
    time_readweb = 0;
    time_writelog = 0;

    if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
        (void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
                    "\tnweb is a small and very safe mini web server\n"
                    "\tnweb only servers out file/web pages with extensions named below\n"
                    "\t and only from the named directory or its sub-directories.\n"
                    "\tThere is no fancy features = safe and secure.\n\n"
                    "\tExample: nweb 8181 /home/nwebdir &\n\n"
                    "\tOnly Supports:", VERSION);
        for(i=0;extensions[i].ext != 0;i++)
            (void)printf(" %s",extensions[i].ext);

        (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
                    "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
                    "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
        exit(0);
    }
    if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
        !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
        !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
        !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
        (void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
        exit(3);
    }
    if(chdir(argv[2]) == -1){
        (void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
        exit(4);
    }
    /* Become deamon + unstopable and no zombies children (= no wait()) */
    if(fork() != 0)
        return 0; /* parent returns OK to shell */
    (void)signal(SIGCLD, SIG_IGN); /* ignore child death */
    (void)signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
    for(i=0;i<32;i++)
        (void)close(i);    /* close open files */
    (void)setpgrp();    /* break away from process group */
    logger(LOG,"nweb starting",argv[1],getpid());
    /* setup the network socket */
    if((listenfd = socket(AF_INET, SOCK_STREAM,0)) < 0)
        logger(ERROR, "system call","socket",0);

    port = atoi(argv[1]);
    if(port < 0 || port >60000)
        logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);

    /* Initialise pthread */
    // pthread_attr_t attr;
    // pthread_attr_init(&attr);
    // pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    // pthread_t pth;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
        logger(ERROR,"system call","bind",0);
    if(listen(listenfd,64) < 0)
        logger(ERROR,"system call","listen",0);

    gettimeofday(&t2, NULL);
    timeslide = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
    (void)sprintf(timecal,"%f",timeslide);
    logger(TIMELOG,"Preparation",timecal,0);

    int num_thread_rdmsg = 3;
    int num_thread_rdfile = 15;
    int num_thread_sdmsg = 40;
    threadpool* pool_rdmsg = initThreadPool(num_thread_rdmsg);
    threadpool* pool_rdfile = initThreadPool(num_thread_rdfile);
    threadpool* pool_sdmsg = initThreadPool(num_thread_sdmsg);
    // file_queue* name_queue = NULL;
    // file_queue* msg_queue = NULL;
    // inti_queue(name_queue);
    // inti_queue(msg_queue);
    // thpool_* thpool = thpool_init(num_thread);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t pth;
    webparam *test = (webparam *)malloc(sizeof(webparam));
    test->pool_rdmsg = pool_rdmsg;
    test->pool_rdfile = pool_rdfile;
    test->pool_sdmsg = pool_sdmsg;
    pthread_create(&pth, &attr, &perform, (void*)test);
    for(hit=1;;hit++) {
        length = sizeof(cli_addr);
        if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR,"system call","accept",0);
        else{
            webparam *param = (webparam *)malloc(sizeof(webparam));
            param->hit = hit;
            param->fd = socketfd;
            param->pool_rdmsg = pool_rdmsg;
            param->pool_rdfile = pool_rdfile;
            param->pool_sdmsg = pool_sdmsg;
            // (void)sprintf(logbuffer,"%d %d %d %d %d %d",max1,min1,max2,min2,max3,min3);
            // logger(LOG,logbuffer,logbuffer,hit);
            // if (max1 < pool_rdmsg->num_working)
            // {
            //     max1 = pool_rdmsg->num_working;
            // }
            // if (max2 < pool_rdfile->num_working)
            // {
            //     max2 = pool_rdfile->num_working;
            // }
            // if (max3 < pool_sdmsg->num_working)
            // {
            //     max3 = pool_sdmsg->num_working;
            // }
            // if (min1 > pool_rdmsg->num_working)
            // {
            //     min1 = pool_rdmsg->num_working;
            // }
            // if (min2 > pool_rdfile->num_working)
            // {
            //     min2 = pool_rdfile->num_working;
            // }
            // if (min3 > pool_sdmsg->num_working)
            // {
            //     min3 = pool_sdmsg->num_working;
            // }
            
            // FILE* maxfile;
            // maxfile = fopen("maxmin.txt","w+");
            // fprintf(maxfile, "%d %d %d %d %d %d\n",max1,min1,max2,min2,max3,min3);
            // fclose(maxfile);
            // printf("%d %d %d %d %d d %d\n",max1,min1,max2,min2,max3,min3);


            addTask2ThreadPool(pool_rdmsg, (void*)read_msg, (void*)param);

        }
    }
    free(test);
    return 0;
}
