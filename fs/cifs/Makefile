# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Linux CIFS/SMB2/SMB3 VFS client
#
ccflags-y += -DDEBUG=1 -I$(src)		# needed for trace events
obj-$(CONFIG_CIFS) += cifs.o

cifs-y := trace.o cifsfs.o cifssmb.o cifs_debug.o connect.o dir.o file.o \
	  inode.o link.o misc.o netmisc.o smbencrypt.o transport.o asn1.o \
	  cifs_unicode.o nterr.o cifsencrypt.o \
	  readdir.o ioctl.o sess.o export.o smb1ops.o winucase.o \
	  smb2ops.o smb2maperror.o smb2transport.o \
	  smb2misc.o smb2pdu.o smb2inode.o smb2file.o

cifs-$(CONFIG_CIFS_XATTR) += xattr.o
cifs-$(CONFIG_CIFS_ACL) += cifsacl.o

cifs-$(CONFIG_CIFS_UPCALL) += cifs_spnego.o

cifs-$(CONFIG_CIFS_DFS_UPCALL) += dns_resolve.o cifs_dfs_ref.o

cifs-$(CONFIG_CIFS_FSCACHE) += fscache.o cache.o

cifs-$(CONFIG_CIFS_SMB_DIRECT) += smbdirect.o
