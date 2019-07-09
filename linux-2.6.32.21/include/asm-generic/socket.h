#ifndef __ASM_GENERIC_SOCKET_H
#define __ASM_GENERIC_SOCKET_H

#include <asm/sockios.h>

/* For setsockopt(2) */
#define SOL_SOCKET	1

#define SO_DEBUG	1      //������TCP �������׽��ַ��ͽ��յ����з�����ϸ������Ϣ ��dmesg������̼�� (int)

#define SO_REUSEADDR	2  //�������ñ��ص�ַ (int) 

#define SO_TYPE		3  //����׽������� (int)  SOCK_STREAM 
#define SO_ERROR	4
#define SO_DONTROUTE	5  //�ƹ����·�ɱ��ѯ (int)
#define SO_BROADCAST	6  //�������ֹ���̷��͹㲥 (int)   ��UDP

//���ڿͻ�SO_RCVBUF������connect֮ǰ����,���ڷ����� SO_RCVBUF��Ҫ��listen()֮ǰ����	
#define SO_SNDBUF	7      //���÷��ͻ�����(int)
#define SO_RCVBUF	8	   //���ý��ջ�����(int) ������MSS��4�� ����TCP���ٻָ��㷨�Ĺ�������

#define SO_SNDBUFFORCE	32
#define SO_RCVBUFFORCE	33

#define SO_KEEPALIVE	9  //����Сʱ�ڸ��׽����κ�һ����û�����ݽ��� tcp���Զ����Զ˷���һ��"���ִ��̽��ֽ�" (int)
						   //ʱ������ /proc/sys/net/ipv4/tcp_keepalive_time 
			               /*
								1) �Զ˷���һ��ack
								2) �Զ˷���һ��RST ��ʾ���̱���
								3) ����Ӧ  �򱾶˼�����̽��ֽ� ��������һ��̽���11��ֻ�û��Ӧ ���������
							*/			
#define SO_OOBINLINE	10 //�ý��յ��Ĵ�������������������������� (int)

#define SO_NO_CHECK	11
#define SO_PRIORITY	12

#define SO_LINGER	13  //struct linger
                       /* ��ѡ��ָ��close���������β��� Ĭ��close��������,���������ݴ��ڷ��ͻ����� ���Ƚ����ݽ��з���*/

#define SO_BSDCOMPAT	14
/* To add :#define SO_REUSEPORT 15 */

#ifndef SO_PASSCRED /* powerpc only differs in these */
#define SO_PASSCRED	16
#define SO_PEERCRED	17

//�������������selectʹ�� ��(���ջ���)���ݵĸ����Ƿ������select����
#define SO_RCVLOWAT	18   //���յ�ˮλ��� (int)  Ĭ��ֵΪ1
#define SO_SNDLOWAT	19   //���͵�ˮλ��� (int)  Ĭ��ֵΪ1


						
#define SO_RCVTIMEO	20   //���ճ�ʱ(timeval{})
#define SO_SNDTIMEO	21   //���ͳ�ʱ(timeval{})
#endif

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		22
#define SO_SECURITY_ENCRYPTION_TRANSPORT	23
#define SO_SECURITY_ENCRYPTION_NETWORK		24

#define SO_BINDTODEVICE	25

/* Socket filtering */
#define SO_ATTACH_FILTER	26
#define SO_DETACH_FILTER	27

#define SO_PEERNAME		28
#define SO_TIMESTAMP		29
#define SCM_TIMESTAMP		SO_TIMESTAMP

#define SO_ACCEPTCONN		30

#define SO_PEERSEC		31
#define SO_PASSSEC		34
#define SO_TIMESTAMPNS		35
#define SCM_TIMESTAMPNS		SO_TIMESTAMPNS

#define SO_MARK			36

#define SO_TIMESTAMPING		37
#define SCM_TIMESTAMPING	SO_TIMESTAMPING

#define SO_PROTOCOL		38
#define SO_DOMAIN		39

#endif /* __ASM_GENERIC_SOCKET_H */
