#ifndef ARCH__X86__MM__KMEMCHECK__SHADOW_H
#define ARCH__X86__MM__KMEMCHECK__SHADOW_H

enum kmemcheck_shadow {
	KMEMCHECK_SHADOW_UNALLOCATED,  //�Ƿ����� δ����ģ���SLAB�У��·����slabҳ����û�б�����object�Ĳ��ֻᱻ���óɴ�״̬��
	KMEMCHECK_SHADOW_UNINITIALIZED,//�Ƿ�����  δ��ʼ���ģ�һ������£��·����ҳ�涼�ᱻ���óɴ�״̬��
	KMEMCHECK_SHADOW_INITIALIZED,//��ʼ���ģ������ķ�������ȷ�ģ�
	KMEMCHECK_SHADOW_FREED,//�Ƿ�����  �ͷŵģ���SLAB�У���object���ͷź�����ռ�õ��ڴ�ᱻ���óɴ�״̬��
};

void *kmemcheck_shadow_lookup(unsigned long address);

enum kmemcheck_shadow kmemcheck_shadow_test(void *shadow, unsigned int size);
void kmemcheck_shadow_set(void *shadow, unsigned int size);

#endif
