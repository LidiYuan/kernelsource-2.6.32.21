#ifndef __ASM_GENERIC_SOCKIOS_H
#define __ASM_GENERIC_SOCKIOS_H

/* Socket-level I/O control calls. */

/*ioctl()  �׽���ѡ��*/ 
#define FIOSETOWN	0x8901       //���ñ��׽��ֵĽ���ID�������ID  int
#define SIOCSPGRP	0x8902       //���ñ��׽��ֵĽ���ID�������ID  int
#define FIOGETOWN	0x8903       //���ر��׽��ֽ���ID�������ID    int
#define SIOCGPGRP	0x8904       //���ر��׽��ֽ���ID�������ID    int
#define SIOCATMARK	0x8905       //�Ƿ�λ�ڴ�����  int  ʹ��sockatmark()���������ioctlѡ��
#define SIOCGSTAMP	0x8906		/* Get stamp (timeval) */
#define SIOCGSTAMPNS	0x8907		/* Get stamp (timespec) */

#endif /* __ASM_GENERIC_SOCKIOS_H */
