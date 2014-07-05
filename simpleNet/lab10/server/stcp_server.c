//�ļ���: server/stcp_server.c
//
//����: ����ļ�����STCP�������ӿ�ʵ��. 
//
//��������: 2013��1��

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"


void replyACK(seg_t *recv,seg_t *send,server_tcb_t *recvTCB,int ackType);
void usleep(unsigned long usec);
//����tcbtableΪȫ�ֱ���
server_tcb_t* tcbtable[MAX_TRANSPORT_CONNECTIONS];
//������SIP���̵�����Ϊȫ�ֱ���
int sip_conn;

/*********************************************************************/
//
//STCP APIʵ��
//
/*********************************************************************/

pthread_t thread;

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL. �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, 
// �ñ�����Ϊsip_sendseg��sip_recvseg���������. ���, �����������seghandler�߳������������STCP��.
// ������ֻ��һ��seghandler.
void stcp_server_init(int conn) 
{
	//init
	int i = 0;
	for(; i < MAX_TRANSPORT_CONNECTIONS;i++)
		tcbtable[i] = NULL;
	//set global variable
	sip_conn = conn;
	int rc = pthread_create(&thread,NULL,seghandler,NULL);
	if(rc)
	{
		printf("Server-init ERROR: return code from pthread_create() is %d\n",rc);
		exit(-1);
	}
	return;
}

// ����������ҷ�����TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��, ����, TCB state������ΪCLOSED, �������˿ڱ�����Ϊ�������ò���server_port. 
// TCB������Ŀ������Ӧ��Ϊ�����������׽���ID�������������, �����ڱ�ʶ�������˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
int stcp_server_sock(unsigned int server_port) 
{
	int i = 0;
	for(; i < MAX_TRANSPORT_CONNECTIONS;i++)
		if(tcbtable[i] == NULL)
		{
			printf("Server-STCP: find an empty TCB, position: %d \n",i);
			break;
		}
	if(i == MAX_TRANSPORT_CONNECTIONS)
		return -1;
	//��ʼ��TCB��Ϣ
	server_tcb_t *newTCB = (server_tcb_t*)malloc(sizeof(server_tcb_t));
	newTCB->state = CLOSED;
	newTCB->server_portNum = server_port;
	newTCB->expect_seqNum = 0;
	newTCB->recvBuf = (char*)malloc(RECEIVE_BUF_SIZE);
	newTCB->usedBufLen = 0;
	newTCB->bufMutex = malloc(sizeof(pthread_mutex_t));
	//��ʼ���������������
	if(pthread_mutex_init(newTCB->bufMutex, PTHREAD_MUTEX_TIMED_NP) != 0)
	{
		printf("fail to create buffer mutex\n");
		exit(-1);
	}
	//����ȫ��TCB����
	tcbtable[i] = newTCB;
	printf("Server-Sock: port(%d) created a new TCB\n",server_port);
	return i;
}

// �������ʹ��sockfd���TCBָ��, �������ӵ�stateת��ΪLISTENING. ��Ȼ��������ʱ������æ�ȴ�ֱ��TCB״̬ת��ΪCONNECTED 
// (���յ�SYNʱ, seghandler�����״̬��ת��). �ú�����һ������ѭ���еȴ�TCB��stateת��ΪCONNECTED,  
// ��������ת��ʱ, �ú�������1. �����ʹ�ò�ͬ�ķ�����ʵ�����������ȴ�.
int stcp_server_accept(int sockfd) 
{
	server_tcb_t *accTCB = tcbtable[sockfd];
	if(accTCB == NULL)
	{
		printf("Server-Accept: accept TCB error\n");
		return -1;
	}
	accTCB->state = LISTENING;
	while(accTCB->state != CONNECTED)
		usleep(ACCEPT_POLLING_INTERVAL/1000);
	printf("Server-Accept: port(%d) connection successfully\n",accTCB->server_portNum);
	return 1;
}

// ��������STCP�ͻ��˵�����. �������ÿ��RECVBUF_POLLING_INTERVALʱ��
// �Ͳ�ѯ���ջ�����, ֱ���ȴ������ݵ���, ��Ȼ��洢���ݲ�����1. ����������ʧ��, �򷵻�-1.
int stcp_server_recv(int sockfd, void* buf, unsigned int length) 
{
	printf("Server-Recv: begin to fetch data\n");
	if(length > RECEIVE_BUF_SIZE)
	{
		printf("Server-Recv: too much data\n");
		return -1;
	}	
	int tag;
	server_tcb_t *recvTCB = tcbtable[sockfd];
	if(recvTCB == NULL)
	{
		printf("Server-Recv: TCB is NULL Error\n");
		return -1;
	}
	while(1)
	{
		pthread_mutex_lock(recvTCB->bufMutex);
		if(recvTCB->usedBufLen >= length)
		{
			tag = 0;
			/*
				int i;
				for(i=0;i<length;i++)
				{
			   		printf("%c",recvTCB->recvBuf[i]);
			  	}
			*/	
			memcpy((char*)buf,recvTCB->recvBuf,length);

			//char *p = recvTCB->recvBuf + length;
			recvTCB->usedBufLen -= length;
			//memcpy(recvTCB->recvBuf,p,recvTCB->usedBufLen);
			for(;tag < recvTCB->usedBufLen;tag++)
			{
				recvTCB->recvBuf[tag] = recvTCB->recvBuf[tag+length];
				//	printf("%c",recvTCB->recvBuf[tag]);
			}
			pthread_mutex_unlock(recvTCB->bufMutex);
			return 1;
		}
		pthread_mutex_unlock(recvTCB->bufMutex);
		sleep(RECVBUF_POLLING_INTERVAL);
	}
}

// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
int stcp_server_close(int sockfd) 
{
	printf("Server-Close: begin to close %d\n",sockfd);
	server_tcb_t* closeTCB = tcbtable[sockfd];
	if(closeTCB == NULL)
	{
		printf("Server-Close: TCB is NULL error\n");
		return -1;
	}
	//wait
	if(closeTCB->state!=CLOSED&&closeTCB->state!=CLOSEWAIT)
		sleep(CLOSEWAIT_TIMEOUT*2);
	if(closeTCB->state!=CLOSED&&closeTCB->state!=CLOSEWAIT)
		return -1;
	sleep(CLOSEWAIT_TIMEOUT);
	closeTCB->state = CLOSED;
	closeTCB->usedBufLen = 0;
	//�ͷŽ��ջ�����
	free(closeTCB->recvBuf);
	//���ٻ������
	pthread_mutex_destroy(closeTCB->bufMutex);
	//�ͷŻ������
	free(closeTCB->bufMutex);
	free(closeTCB);
	tcbtable[sockfd] = NULL;
	return 1;
}

// ������stcp_server_init()�������߳�. �������������Կͻ��˵Ľ�������. seghandler�����Ϊһ������sip_recvseg()������ѭ��, 
// ���sip_recvseg()ʧ��, ��˵����SIP���̵������ѹر�, �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���.
// ��鿴�����FSM���˽����ϸ��.
void* seghandler(void* arg) 
{
	int i;
	seg_t *seg = (seg_t*)malloc(sizeof(seg_t));
	seg_t *segAck = (seg_t*)malloc(sizeof(seg_t));
	server_tcb_t *recvTCB = NULL;
	int src_nodeID = 0;
	while(1)
	{
		recvTCB = NULL;
		memset(seg,0,sizeof(seg_t));
		memset(segAck,0,sizeof(seg_t));
		i = sip_recvseg(sip_conn,&src_nodeID,seg);
		if(i < 0)	//seg lost
		{
			printf("Server-Seghandler: Seg Lost!\n");
			continue;
		}
		if(checkchecksum(seg) == -1)
		{
			printf("Server-Seghandler Checksum Error!\n");
			continue;
		}
		printf("Server-Seghandler: port(%d) received a seg\n",seg->header.dest_port);

		for(i = 0; i < MAX_TRANSPORT_CONNECTIONS;i++)
		{
			if(tcbtable[i] != NULL && tcbtable[i]->server_portNum == seg->header.dest_port) 
			{
				recvTCB = tcbtable[i];
				break;
			}
		}

		if(recvTCB == NULL)
		{
			printf("Server-Seghandler: Can't find the TCB item\n");
			continue;
		}
		switch(recvTCB->state)
		{
			case CLOSED:
				printf("Server-State: CLOSED\n");
				break;
			case LISTENING:
				printf("Server-State: LISTENING\n");
				if(seg->header.type == SYN)
				{
					printf("Server-State: LISTENING got SYN\n");
					recvTCB->client_portNum = seg->header.src_port;
					recvTCB->expect_seqNum = seg->header.seq_num;
					recvTCB->client_nodeID = src_nodeID;
					replyACK(seg,segAck,recvTCB,SYNACK);
					recvTCB->state = CONNECTED;
				}
				break;
			case CLOSEWAIT:
				printf("Server-State: CLOSEWAIT\n");
				if(seg->header.type == FIN)
				{
					printf("Server-State: CLOSEWAIT got FIN\n");
					recvTCB->client_nodeID = src_nodeID;
					replyACK(seg,segAck,recvTCB,FINACK);
					recvTCB->state = CLOSEWAIT;
				}
				break;
			case CONNECTED:
				printf("Server-State: CONNECTED\n");
				if(seg->header.type == SYN)
				{
					printf("Server-State: CONNECTED got SYN\n");
					recvTCB->client_portNum = seg->header.src_port;
					recvTCB->expect_seqNum = seg->header.seq_num;
					recvTCB->client_nodeID = src_nodeID;
					replyACK(seg,segAck,recvTCB,SYNACK);
					recvTCB->state = CONNECTED;
				}
				else if(seg->header.type == FIN)
				{
					recvTCB->client_nodeID = src_nodeID;
					printf("Server-State: CONNECTED got FIN\n");
					if(recvTCB->expect_seqNum != seg->header.seq_num)
					{
						printf("Server-FIN: seq Error!");
					}
					else
					{
						replyACK(seg,segAck,recvTCB,FINACK);
						recvTCB->state = CLOSEWAIT;
					}

				}
				else if(seg->header.type == DATA)
				{
					printf("Server-State: CONNECTED got DATA\n");
					if(recvTCB->expect_seqNum == seg->header.seq_num)
					{
						printf("Server-DATA: sequence equal, then save data\n");
						pthread_mutex_lock(recvTCB->bufMutex);
						printf("data length:%d\n",seg->header.length);
						memcpy(recvTCB->recvBuf+recvTCB->usedBufLen,seg->data,seg->header.length);
						recvTCB->usedBufLen += seg->header.length;
						
						recvTCB->expect_seqNum =(recvTCB->expect_seqNum+seg->header.length)%MAX_SEQNUM;
						pthread_mutex_unlock(recvTCB->bufMutex);

						replyACK(seg,segAck,recvTCB,DATAACK);
					}
					else
					{
						printf("Server-DATA: sequence not equal\n");

						segAck->header.type = DATAACK;
						segAck->header.dest_port = recvTCB->client_portNum;
						segAck->header.src_port = recvTCB->server_portNum;
						segAck->header.ack_num = recvTCB->expect_seqNum;;
						segAck->header.checksum = checksum(segAck);

						sip_sendseg(sip_conn,src_nodeID,segAck);
						printf("Server-DATA: reply ACK to client(%d)\n",recvTCB->client_portNum);
					}
				}
		}

	}
	return 0;
}


void replyACK(seg_t *recv,seg_t *send,server_tcb_t *recvTCB,int ackType) 
{
	printf("Server-ACK: begin to send ACk to client(%d)\n",recvTCB->client_portNum);

	send->header.type = ackType;
	send->header.dest_port = recvTCB->client_portNum;
	send->header.src_port = recvTCB->server_portNum;
	send->header.ack_num = recvTCB->expect_seqNum;
	memset(&(send->header.checksum),0,sizeof(unsigned int));
	send->header.checksum = checksum(send);

	sip_sendseg(sip_conn,recvTCB->client_nodeID,send);
	printf("Server-ACK: finish sending ACK to client(%d)\n",recvTCB->client_portNum);
}