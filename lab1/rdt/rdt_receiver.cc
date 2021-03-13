/*
 * @Description: 
 * @Version: 1.0
 * @Author: Zhang AO
 * @studentID: 518021910368
 * @School: SJTU
 * @Date: 2021-03-05 13:44:31
 * @LastEditors: Seven
 * @LastEditTime: 2021-03-13 10:58:46
 */
/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 * NOTE: This implementation assumes there is no packet loss, corruption, or 
 *       reordering.  You will need to enhance it to deal with all these 
 *       situations.  In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-  1 byte  ->|<-             the rest            ->|
 *       | payload size |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <list>
#include <cassert>
#include <iostream>

#include "rdt_struct.h"
#include "rdt_receiver.h"
using namespace std;

#define MAX_WINDOW_SIZE 10
#define HEADER_SIZE 5
#define MAX_SEQ 60
#define A_CONST 7
#define MAX_PAYLOAD_SIZE (RDT_PKTSIZE - HEADER_SIZE)

/*
*包头设计：
*   |<- checksum ->|<-payload size->|<-sequence number->|<- a constant ->|
*   |<-  2 byte  ->|<-   1 byte   ->|<-     1 byte    ->|<-   1 byte   ->|
*/

//自定义收到的packet结构体，reciever端中windows的单元
struct RecievePacket
{
    bool recieved = false;
    char *data = NULL;
    int size = 0;
    char seq = 0;
    bool if_head = false; //是否是一个msg的头
    bool if_tail = false; //是否是一个msg的尾
    RecievePacket() {}
    ~RecievePacket()
    {
        if (data)
        {
            free(data);
            data = NULL;
        }
    }
};

static int window_cursor;
static RecievePacket windows[MAX_WINDOW_SIZE]; //packet 缓存，窗口大小与sender一致
static message cur_Msg;                        //当前正在合并的message信息
static char expect_seq;                        //期待的最小的序号

/**
 * @description: 增加sequence number
 * @param {char} &seq
 * @return {*}
 * @author: Zhang Ao
 */
static void Seq_increase(char &seq)
{
    seq = (seq + 1) % MAX_SEQ;
}

static void flush_Msg()
{
    assert(cur_Msg.data);
    assert(cur_Msg.size);
    Receiver_ToUpperLayer(&cur_Msg);
    free(cur_Msg.data);
    cur_Msg.data = NULL;
    cur_Msg.size = 0;
}

/**
 * @description: 因特网校验码验算方法
 * @param {struct packet} *p
 * @return {*}
 * @author: Zhang Ao
 */
static short checksum(struct packet *p)
{
    unsigned long c_sum = 0;
    int size = p->data[2];                   //有效载荷大小
    for (int i = 2; i < RDT_PKTSIZE; i += 2) //除去checksum位的数据，其他位的数据每两个byte求和
    {
        c_sum += *(short *)(&p->data[i]);
    }
    while (c_sum >> 16) //高位byte与地位byte相加
        c_sum = (c_sum >> 16) + (c_sum & 0xffff);
    return ~c_sum;
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    cur_Msg.data = NULL;
    cur_Msg.size = 0;
    window_cursor = 0;
    expect_seq = 0;
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some 
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    printf("reciever finally\n");

    for (int i = 0; i < MAX_WINDOW_SIZE; i++)
    {
        if (windows[i].data)
        {
            free(windows[i].data);
            windows[i].data = NULL;
        }
    }
    if (cur_Msg.data)
    {
        free(cur_Msg.data);
        cur_Msg.data = NULL;
    }
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
}

/**
 * @description: 移动windows时，将packet合并成message信息并向上抛出
 * @param {structRecievePacket} *p
 * @return {*}
 * @author: Zhang Ao
 */
void Reciever_AddtoMsg(struct RecievePacket *p)
{
    printf("\treciever add to msg ——seq=%d\n", p->seq);
    assert(p->seq == expect_seq);
    assert(p->recieved && p->data);
    if (p->seq == 0)
    {
        printf("\t\t attention！\n");
    }
    if (p->if_head) //message头部
    {
        //来了一个新的message的信息，上一个message必须已经被flush掉了
        assert(cur_Msg.data == NULL && cur_Msg.size == 0);
        int new_size = p->size;
        cur_Msg.data = (char *)malloc(new_size);
        memcpy(cur_Msg.data, p->data, new_size);
        cur_Msg.size = new_size;
        if (p->if_tail)
        { //同时也是尾部，上抛并重置msg
            printf("\treciever combined one message with seq=%d, and size=%d\n", p->seq, cur_Msg.size);
            flush_Msg();
        }
    }
    else
    {
        assert(cur_Msg.data && cur_Msg.size > 0);
        //拼接message
        int new_size = cur_Msg.size + p->size;
        cur_Msg.data = (char *)realloc(cur_Msg.data, new_size);
        memcpy(cur_Msg.data + cur_Msg.size, p->data, p->size);
        cur_Msg.size = new_size;
        if (p->if_tail)
        {
            printf("\treciever combined one message with seq=%d, and size=%d\n", p->seq, cur_Msg.size);
            flush_Msg();
        }
    }
    //清空packet
    if (p->data)
    {
        free(p->data);
        p->data = NULL;
    }
    p->recieved = false;
    Seq_increase(expect_seq);
    printf("\treciever: \twaiting for seq=%d\n", expect_seq);
}

/**
 * @description: 发送ack包
 * @param {char} seq
 * @return {*}
 * @author: Zhang Ao
 */
void Reicever_SendACK(char seq) //此处seq只含有低6位bit
{
    printf("reciever send ack seq=%d\n", seq);
    assert(seq >> 6 == 0);
    packet ack;
    ack.data[2] = 0;
    ack.data[3] = seq;
    ack.data[4] = A_CONST;
    *(short *)ack.data = checksum(&ack);
    Receiver_ToLowerLayer(&ack);
}

/* event handler, called when a packet is passed from the lower layer at the 
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{
    //检验校验码
    short sum = *(short *)(pkt->data);
    if (sum != checksum(pkt) || pkt->data[4] != A_CONST)
        return;

    char seq = pkt->data[3] & 63; //取低位6个bit

    char size = pkt->data[2];
    if (seq < 0 || seq >= 60 || size <= 0 || size > MAX_PAYLOAD_SIZE)
        return;
    printf("recieve from low——get pkt seq=%d\n", seq);
    // printf("recieve from low——get pkt size=%d\n", size);
    //看看是否是一个msg的头或者尾
    bool if_head = pkt->data[3] >> 7;
    bool if_tail = (pkt->data[3] >> 6) & 1;

    //校验码通过，发送ack
    Reicever_SendACK(seq);
    //最大只能是expect_seq+9,下面的表达式考虑了seq最大只能是59
    int difference = seq - expect_seq;
    if ((difference >= -50 && difference < 0) || difference >= 10)
    {
        printf("\t\t\t didn't modify 1 and waiting for seq=%d\n", expect_seq);
        return;
    }

    RecievePacket *p = &windows[seq % MAX_WINDOW_SIZE];
    if (p->recieved) //收到过了，直接返回
    {
        assert(p->data);
        return;
    }
    //还没收到
    assert(p->data == NULL);
    p->if_head = if_head;
    p->seq = seq;
    p->if_tail = if_tail;
    p->recieved = true;
    p->data = (char *)malloc(size);
    memcpy(p->data, pkt->data + HEADER_SIZE, size);
    p->size = size;
    //如果收到了window中的第一个packet，可以移动window，并加入message
    if (seq % MAX_WINDOW_SIZE == window_cursor)
    {
        //将可以合并的packet加入windows中
        Reciever_AddtoMsg(p); //p已经被free了
        int i = 1;
        //只需要顺序往后，碰到没有recieved的包就可以直接中断。
        while (i < MAX_WINDOW_SIZE)
        {
            p = &windows[(i + window_cursor) % MAX_WINDOW_SIZE];
            if (p->recieved)
            {
                Reciever_AddtoMsg(p);
                i++;
                continue;
            }
            break;
        }
        //移动window指针
        window_cursor = (window_cursor + i) % MAX_WINDOW_SIZE;
    }
}