#
# Makefile for the linux journaling routines.
#

#这行的意思是你要将jbd2模块以什么样的方式编译到内核中 其中CONFIG_JBD2是在config中存在的 
#有三种选择方式（Y与内核一起编译,M以模块方式编译，N不编译，当内核有Ext4或是OCFS2文件系统时不能没有jbd2）
obj-$(CONFIG_JBD2) += jbd2.o

#这行的意思是jbd2所需的目标文件，一共有六个文件
jbd2-objs := transaction.o commit.o recovery.o checkpoint.o revoke.o journal.o
