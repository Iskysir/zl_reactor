#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <assert.h>
#include <errno.h>
#include "net/PipePairFactory.h"

//eventfd与pipe类似，可以用来完成两个线程之间事件触发；甚至进程间消息响应
//eventfd优点：
//1. 比pipe少用了一个文件描述符;
//2. eventfd是固定大小缓冲区（8 bytes），而pipe是可变大小的

//eventfd用法总结
//1. 如果write过快，那么read时会一次性读取之前write数据的总和，也即：
//   在write之后没有read，但是又write新的数据，那么读取的是这两次的8个字节的和
//   即 如果先write 1 2 3，之后才read那么read的值是1+2+3=6。

// TODO :本处例子似乎有问题

#define TEST_EventFdPairFactory

int g_eventfd = -1;
zl::net::EventFdPairFactory g_efdFactory;

void *read_thread(void *dummy)
{
    int epoll_fd = epoll_create(1024);
    if (epoll_fd < 0)
    {
        perror("epoll_create fail: ");
        return NULL;
    }

    struct epoll_event read_event;
    read_event.events = EPOLLHUP | EPOLLERR | EPOLLIN/* | EPOLLOUT*/;
#ifdef TEST_EventFdPairFactory
    int fd = g_efdFactory.readFd();
#else
    int fd = g_eventfd;
#endif
    read_event.data.fd = fd;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &read_event);
    if (ret < 0)
    {
        perror("epoll ctl failed:");
        return NULL;
    }

    // sleep(8);  //等主线程写完
    const static size_t max_events = 10;
    struct epoll_event events[max_events];
    while (1)
    {
        ret = epoll_wait(epoll_fd, events, max_events, 6000);
        //sleep(1);
        if (ret > 0)
        {
            printf("epoll_wait [%d]\n", ret);
            for (int i = 0; i < ret; i++)
            {
                if (events[i].events & EPOLLHUP)
                {
                    printf("epoll eventfd has epoll hup.\n");
                    break;
                }
                else if (events[i].events & EPOLLERR)
                {
                    printf("epoll eventfd has epoll error.\n");
                    break;
                }
                else if (events[i].events & (EPOLLIN | EPOLLPRI))
                {
                    int event_fd = events[i].data.fd;
                    uint64_t count = 0;
                    //size_t n = ::read(event_fd, &count, sizeof(count));
                #ifdef TEST_EventFdPairFactory
                    size_t n = g_efdFactory.read(&count, sizeof(count));
                #else
                    size_t n = ::read(event_fd, &count, sizeof(count));
                #endif
                    assert(n == sizeof(uint64_t) && "eventfd every read sizeof(uint64_t) data\n");
                    if (n < 0)
                    {
                        perror("read fail1:");
                        break;
                    }
                    else if(n == 0)
                    {
                        perror("read fail2:");
                        break;
                    }
                    else
                    {
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        printf("success  read, read %d bytes(%llu) at %lds %ldus\n", n, count, tv.tv_sec, tv.tv_usec);
                    }
                }
                ////下面两个分支取消之后就无法write-read交互了，
                //else if (events[i].events & EPOLLOUT)   
                //{
                //    printf("EPOLLOUT [%d][%d][%d]\n", ret, errno, events[i].events);
                //}
                //else   // occur some errors[8][0][0]  why????
                //{
                //    printf("occur some errors[%d][%d][%d]\n", ret, errno, events[i].events);
                //}
            }
        }
        else if (ret == 0)
        {
            printf("epoll wait timed out.\n");
            continue;
        }
        else
        {
            perror("epoll wait error:");
            break;
        }
    }

    if (epoll_fd >= 0)
    {
        close(epoll_fd);
        epoll_fd = -1;
    }

    return NULL;
}


int main(int argc, char *argv[])
{
    pthread_t pid = 0;

    #ifdef TEST_EventFdPairFactory
    //g_eventfd = g_efdFactory.readFd();
    #else
    g_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_eventfd < 0)
    {
        perror("eventfd failed.");
        return 0;
    }
    #endif

    printf("g_efdFactory [%d][%d]\n", g_efdFactory.readFd(), g_efdFactory.writeFd());

    int ret = pthread_create(&pid, NULL, read_thread, NULL);
    if (ret < 0)
    {
        perror("pthread create:");
        close(g_eventfd);
        return 0;
    }

    for (uint64_t i = 1; i <= 5; i++)
    {
        sleep(1);   //如果取消，那么这里很快write完成，可epoll线程只能读到一次数据
    #ifdef TEST_EventFdPairFactory
        ret = g_efdFactory.write(&i, sizeof(i));
    #else
        ret = ::write(g_eventfd, &i, sizeof(i));
    #endif
        if (ret < 0)
        {
            perror("write event fd fail:");
            break;
        }
        else
        {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            printf("success write, write %d bytes(%llu) at %lds %ldus\n", ret, i, tv.tv_sec, tv.tv_usec);
        }
    }

    printf("send data over\n");

    pthread_join(pid, NULL);
    return 0;
}
