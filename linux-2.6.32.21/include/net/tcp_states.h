/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol sk_state field.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_TCP_STATES_H
#define _LINUX_TCP_STATES_H

//tcp����״̬
//������A��Ϊ�رյķ���˼��׸�����FIN���ĵ�һ��
enum {
	TCP_ESTABLISHED = 1,//�ͻ��˽��յ�֮ǰ���͵�SYN��ACK�� ���ô�״̬,�������ŷ���һ��ACK�������,����ʱSYN_RECV״̬�ķ�����յ���ACK��Ҳ�������״̬
	TCP_SYN_SENT, //�ͻ��˷���һ��SYN�������������������Ӻ� ���ø�״̬,������ʾ���͵�SYN ����δ�õ�Ӧ�� ���õ�Ӧ�����TCP_ESTABLISHED
	TCP_SYN_RECV,//������ڽ��յ��ͻ��˷��͵�SYN �ᷢ��һ������ACK��SYN���ĸ��ͻ���,֮�����ô�״̬
	TCP_FIN_WAIT1,//A�˷���رղ���,A�˽�����һ��FIN���Է� ����״̬����ΪTCP_FIN_WAIT1
	TCP_FIN_WAIT2,//����TCP_FIN_WAIT1״̬��A�����ڽ��յ�֮ǰ���͵�FIN��ACK���ĺ���״̬����ΪTCP_FIN_WAIT2,Ȼ��һֱ���ڴ�״̬,ֱ�����յ��Է����͵�FIN
                  //�ڽ��յ��Է���FIN֮ǰ �˶�һֱ���Խ��б��ĵĽ���,A�˷���FIN������ʾ A�˲����ٷ��ͱ���,�������Խ��ձ���
                  
	TCP_TIME_WAIT,//TCP_FIN_WAIT2״̬��A���ڽ��յ��Է���FIN�����TCP_TIME_WAIT״̬��ʾ���ȴ�2MSL
	TCP_CLOSE,     //�ر�״̬,���������ֻ��ƽ���ǰ���ڴ�״̬
	TCP_CLOSE_WAIT,//B���ڽ��յ�A�˵�FIN����ʱ,������״̬����Ϊ��״̬ ,������ACK��A��,�˺�B����Ȼ���Է���û�з���������ݸ�A��
	TCP_LAST_ACK,//B���ڷ���FIN��A�˺���״̬����ΪLAST_ACK�ȴ�A�˵�ACK���ģ�һ�����ܵ�ACK��ر� B�˽���CLOSED
	TCP_LISTEN, //����״̬����Է����  ��ʾ���ڵȴ��ͻ�����������
	TCP_CLOSING,	/* ������˫��ͬʱ����FIN���Ļ�����״̬(��ͬʱִ��close)*/

	TCP_MAX_STATES	/* Leave at the end! */
};

#define TCP_STATE_MASK	0xF

#define TCP_ACTION_FIN	(1 << 7)

enum {
	TCPF_ESTABLISHED = (1 << 1),
	TCPF_SYN_SENT	 = (1 << 2),
	TCPF_SYN_RECV	 = (1 << 3),
	TCPF_FIN_WAIT1	 = (1 << 4),
	TCPF_FIN_WAIT2	 = (1 << 5),
	TCPF_TIME_WAIT	 = (1 << 6),
	TCPF_CLOSE	 = (1 << 7),
	TCPF_CLOSE_WAIT	 = (1 << 8),
	TCPF_LAST_ACK	 = (1 << 9),
	TCPF_LISTEN	 = (1 << 10),
	TCPF_CLOSING	 = (1 << 11) 
};

#endif	/* _LINUX_TCP_STATES_H */
