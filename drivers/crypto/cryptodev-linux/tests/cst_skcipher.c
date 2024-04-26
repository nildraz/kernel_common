/*
 * Demo on how to use /dev/crypto device for ciphering.
 *
 * Placed under public domain.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include <sys/socket.h>

#include <crypto/cryptodev.h>
#include "testhelper.h"

#define MBytes 		(1024*1024)
#define KBytes 		1024
#define HANDLE_BLOCK 	128*KBytes

#define HASH_MAX_LEN 	64

#define EVP_MAX_IV_LENGTH 64

#define ARRAY_SIZE(_x_) (sizeof(_x_) / sizeof(_x_[0]))

static unsigned char key[64] = {
	0xbb, 0xbb, 0xbb, 0xcd, 0xce, 0xcf, 0xca, 0x2b,
	0x2c, 0x2d, 0x0e, 0x0f, 0x0a, 0x0b, 0x0c, 0x0d,
	0x1a, 0x1a, 0x1a, 0x1b, 0x0b, 0x0b, 0x0c, 0x0c,
	0x1a, 0x1a, 0x1a, 0x1b, 0x0b, 0x0b, 0x0c, 0x0c,
	0xbb, 0xbb, 0xbb, 0xcd, 0xce, 0xcf, 0xca, 0x2b,
	0x2c, 0x2d, 0x0e, 0x0f, 0x0a, 0x0b, 0x0c, 0x0d,
};
static unsigned char iv[16] = {
	0x1a, 0x1a, 0x1a, 0x1b, 0x0b, 0x0b, 0x0c, 0x0c,
	0x1a, 0x1a, 0x1a, 0x1b, 0x0b, 0x0b, 0x0c, 0x0c,
};
 
static int test_data_size[] = 
	{ 16*16, 16*32, 1*KBytes, 2*KBytes, 4*KBytes, 8*KBytes, 16*KBytes, 32*KBytes, 1*MBytes, 
	  16*MBytes, 32*MBytes, 64*MBytes, 128*MBytes
	};
static int test_data_len = ARRAY_SIZE(test_data_size);
static FILE *g_fp;

#define SINGLE_MAX_SIZE (32 * KBytes)
#define XTS_MAX_SIZE (0x2000)

struct dev_crypto_state {
    struct session_op d_sess;
    int d_fd;
# ifdef USE_CRYPTODEV_DIGESTS
    char dummy_mac_key[HASH_MAX_LEN];
    unsigned char digest_res[HASH_MAX_LEN];
    char *mac_data;
    int mac_len;
# endif
};
struct dev_crypto_ctx {
	unsigned char iv[EVP_MAX_IV_LENGTH];
	unsigned char iv_len;
	unsigned char encrypt;
	struct dev_crypto_state state;
};

static struct dev_crypto_ctx g_ctx1;
static struct dev_crypto_ctx g_ctx2;

struct test_alg {
	char *salg_name;
	int alg_type;
	int key_size;
	int block_size;
	int iv_size;
};

static struct test_alg test_algs[] = {
	/* aes */
	/* no iv */
	/* 0  no iv */
	{ 
		.salg_name = "ecb(aes)",
		.alg_type = CRYPTO_AES_ECB,
		.key_size = 16,
		.block_size = 16,
		.iv_size = 0,
	},
	{ 
		.salg_name = "ecb(aes)",
		.alg_type = CRYPTO_AES_ECB,
		.key_size = 24,
		.block_size = 24,
		.iv_size = 0,
	},
	{ 
		.salg_name = "ecb(aes)",
		.alg_type = CRYPTO_AES_ECB,
		.key_size = 32,
		.block_size = 32,
		.iv_size = 0,
	},
	/* 1 */
	/* iv: 16 bytes */
	{ 
		.salg_name = "cbc(aes)",
		.alg_type = CRYPTO_AES_CBC,
		.key_size = 16,
		.block_size = 16,
		.iv_size = 16,
	},
	{ 
		.salg_name = "cbc(aes)",
		.alg_type = CRYPTO_AES_CBC,
		.key_size = 24,
		.block_size = 24,
		.iv_size = 16,
	},
	{ 
		.salg_name = "cbc(aes)",
		.alg_type = CRYPTO_AES_CBC,
		.key_size = 32,
		.block_size = 32,
		.iv_size = 16,
	},
	/* 3 */
	/* iv: 16 bytes */
	{ 
		.salg_name = "ctr(aes)",
		.alg_type = CRYPTO_AES_CTR,
		.key_size = 16,
		.block_size = 16,
		.iv_size = 16,
	},
	{ 
		.salg_name = "ctr(aes)",
		.alg_type = CRYPTO_AES_CTR,
		.key_size = 24,
		.block_size = 24,
		.iv_size = 16,
	},
	{ 
		.salg_name = "ctr(aes)",
		.alg_type = CRYPTO_AES_CTR,
		.key_size = 32,
		.block_size = 32,
		.iv_size = 16,
	},
	/* 6 */
	{ 
		.salg_name = "xts(aes)",
		.alg_type = CRYPTO_AES_XTS,
		.key_size = 32,
		.block_size = 16,
		.iv_size = 16,
	},
	{ 
		.salg_name = "xts(aes)",
		.alg_type = CRYPTO_AES_XTS,
		.key_size = 48,
		.block_size = 16,
		.iv_size = 16,
	},
	{ 
		.salg_name = "xts(aes)",
		.alg_type = CRYPTO_AES_XTS,
		.key_size = 64,
		.block_size = 16,
		.iv_size = 16,
	},
	/* 8 */
	{ 
		.salg_name = "ofb(aes)",
		.alg_type = CRYPTO_AES_OFB,
		.key_size = 16,
		.block_size = 16,
		.iv_size = 16,
	},
	{ 
		.salg_name = "ofb(aes)",
		.alg_type = CRYPTO_AES_OFB,
		.key_size = 24,
		.block_size = 24,
		.iv_size = 16,
	},
	{ 
		.salg_name = "ofb(aes)",
		.alg_type = CRYPTO_AES_OFB,
		.key_size = 32,
		.block_size = 32,
		.iv_size = 16,
	},
	/* 10 */
	{ 
		.salg_name = "ofb(multi2)",
		.alg_type = CRYPTO_MULTI2_OFB,
		.key_size = 41,
		.block_size = 8,
		.iv_size = 8,
	},
	/* 11 */
	{ 
		.salg_name = "cbc(multi2)",
		.alg_type = CRYPTO_MULTI2_CBC,
		.key_size = 41,
		.block_size = 8,
		.iv_size = 8,
	},
	/* ================================================== */
	/* des */
	/* 0 */
	{ 
		.salg_name = "ecb(des)",
		.alg_type = CRYPTO_DES_ECB,
		.key_size = 8,
		.block_size = 8,
		.iv_size = 0,
	},
	/* 1 */
	{ 
		.salg_name = "ecb(des3_ede)",
		.alg_type = CRYPTO_3DES_ECB,
		.key_size = 24,
		.block_size = 8,
		.iv_size = 0,
	},
	/* 2 */
	{ 
		.salg_name = "cbc(des)",
		.alg_type = CRYPTO_DES_CBC,
		.key_size = 8,
		.block_size = 8,
		.iv_size = 8,
	},
	/* 3 */
	{ 
		.salg_name = "cbc(des3_ede)",
		.alg_type = CRYPTO_3DES_CBC,
		.key_size = 24,
		.block_size = 8,
		.iv_size = 8,
	},
};
static int test_algs_size = ARRAY_SIZE(test_algs);

static int
cryptodev_cipher(struct dev_crypto_ctx *ctx, unsigned char *out,
                 const unsigned char *in, size_t inl)
{
	struct crypt_op cryp;
	struct dev_crypto_state *state = &ctx->state;
	struct session_op *sess = &state->d_sess;
	const void *iiv;
	unsigned char save_iv[EVP_MAX_IV_LENGTH];

	if (state->d_fd < 0)
		return (-1);
	if (!inl)
		return (0);

	memset(&cryp, 0, sizeof(cryp));

	cryp.ses = sess->ses;
	cryp.flags = 0;
	cryp.len = inl;
	cryp.src = (caddr_t) in;
	cryp.dst = (caddr_t) out;
	cryp.mac = 0;

	cryp.op = ctx->encrypt ? COP_ENCRYPT : COP_DECRYPT;

	if (ctx->iv_len) {
	    cryp.iv = (caddr_t) ctx->iv;
	    if (!ctx->encrypt) {
		    iiv = in + inl - ctx->iv_len;
		    memcpy(save_iv, iiv, ctx->iv_len);
	    }
	} else
	    cryp.iv = NULL;

	if (ioctl(state->d_fd, CIOCCRYPT, &cryp) == -1) {
		/*
		 * XXX need better errror handling this can fail for a number of
		 * different reasons.
		 */
		return (-1);
	}

	if (ctx->iv_len) {
		if (ctx->encrypt)
			iiv = out + inl - ctx->iv_len;
		else
			iiv = save_iv;
		memcpy(ctx->iv, iiv, ctx->iv_len);
	}
	return (0);
}

static int open_dev_crypto(void)
{
    static int fd = -1;

    if (fd == -1) {
        if ((fd = open("/dev/crypto", O_RDWR, 0)) == -1)
            return (-1);
        /* close on exec */
        if (fcntl(fd, F_SETFD, 1) == -1) {
            close(fd);
            fd = -1;
            return (-1);
        }
    }
    return (fd);
}

static int get_dev_crypto(void)
{
    int fd, retfd;

    if ((fd = open_dev_crypto()) == -1)
        return (-1);
# ifndef CRIOGET_NOT_NEEDED
    if (ioctl(fd, CRIOGET, &retfd) == -1)
        return (-1);

    /* close on exec */
    if (fcntl(retfd, F_SETFD, 1) == -1) {
        close(retfd);
        return (-1);
    }
# else
    retfd = fd;
# endif
    return (retfd);
}

static void put_dev_crypto(int fd)
{
# ifndef CRIOGET_NOT_NEEDED
    close(fd);
# endif
}

int single_alg_test(int len_vector, int sa_vector)
{
	int	ret = -1;
	int	z;
	int	single_size;
	int	real_size;
	int	passed_cnt;
	int	total;

	unsigned char *encrypt_in;
	unsigned char *encrypt_out;
	unsigned char *decrypt_out;

	/* init */
	struct dev_crypto_state *state1 = &g_ctx1.state;
	struct dev_crypto_state *state2 = &g_ctx2.state;
	struct session_op *sess1 = &state1->d_sess;
	struct session_op *sess2 = &state2->d_sess;

	memset(&g_ctx1, 0, sizeof(struct dev_crypto_ctx));
	memset(&g_ctx2, 0, sizeof(struct dev_crypto_ctx));
	memset(state1, 0, sizeof(struct dev_crypto_state));
	memset(state2, 0, sizeof(struct dev_crypto_state));

	memset(sess1, 0, sizeof(struct session_op));
	memset(sess2, 0, sizeof(struct session_op));

	if ((state1->d_fd = get_dev_crypto()) < 0) {
		printf("cryptodev cipher init: Can't get Dev \n"); 
		return (-1);
	}
	state2->d_fd = state1->d_fd;

	sess1->key = (caddr_t) key;
	sess1->keylen = test_algs[sa_vector].key_size;
	sess1->cipher = test_algs[sa_vector].alg_type;

	sess2->key = (caddr_t) key;
	sess2->keylen = test_algs[sa_vector].key_size;
	sess2->cipher = test_algs[sa_vector].alg_type;

	if (ioctl(state1->d_fd, CIOCGSESSION, sess1) == -1) {
		put_dev_crypto(state1->d_fd);
		state1->d_fd = -1;
		return (-1);
	}
	if (ioctl(state2->d_fd, CIOCGSESSION, sess2) == -1) {
		put_dev_crypto(state2->d_fd);
		state2->d_fd = -1;
		return (-1);
	}


	total = test_data_size[len_vector];
	encrypt_in  = malloc(total);
	encrypt_out = malloc(total);
	decrypt_out = malloc(total);

	if (encrypt_in == NULL || encrypt_out == NULL ||
		decrypt_out == NULL) {
		printf("malloc error");
		goto ext;
	}

	memcpy(g_ctx1.iv, iv, 16);
	memcpy(g_ctx2.iv, iv, 16);
	g_ctx1.iv_len = test_algs[sa_vector].iv_size;
	g_ctx1.encrypt = 1;
	g_ctx2.iv_len = test_algs[sa_vector].iv_size;
	g_ctx2.encrypt = 0;

	passed_cnt = 0;

	printf(" ######## name:%s key_size:%d iv_size:%d ##########\n",
		test_algs[sa_vector].salg_name, sess1->keylen, g_ctx1.iv_len);
#if 1
	single_size = total;
#else
	single_size = test_algs[sa_vector].block_size;
#endif
	if (single_size > SINGLE_MAX_SIZE)
		single_size = SINGLE_MAX_SIZE;

	if (!strcmp(test_algs[sa_vector].salg_name, "xts(aes)"))
		if (single_size > XTS_MAX_SIZE)
			single_size = XTS_MAX_SIZE;

	printf("single_size:%d total:%d\n", single_size, total);
	fseek(g_fp, 0L, SEEK_SET);

	for (z = 0; z < total; ) {
		
		if ((real_size = fread(encrypt_in + passed_cnt, 1, single_size, g_fp) != single_size)) {
			printf("real_size %d, single_size:%d\n", real_size, single_size);
			printf("Read file failed\n");
			goto ext;
		}

		ret = cryptodev_cipher(&g_ctx1, encrypt_out + passed_cnt,  encrypt_in + passed_cnt, single_size);
		if (ret < 0) {
			printf("do_encrypt error\n");
			goto ext;
		}
		/* printf("do_encrypt ok\n"); */
		ret = cryptodev_cipher(&g_ctx2, decrypt_out + passed_cnt,  encrypt_out + passed_cnt, single_size);
		if (ret < 0) {
			printf("do_encrypt error\n");
			goto ext;
		}
		/* printf("do_decrypt ok\n"); */
		if (memcmp(decrypt_out + passed_cnt, encrypt_in + passed_cnt, single_size))
			printf("compare fail!\n");

		passed_cnt += single_size;
		z += single_size;
	}
	ret = 0;
ext:
	if (encrypt_in != NULL)
		free(encrypt_in);
	if (encrypt_out != NULL)
		free(encrypt_out);
	if (decrypt_out != NULL)
		free(decrypt_out);

	/* clean up */

	if (ioctl(state1->d_fd, CIOCFSESSION, &sess1->ses) == -1) {
		ret = 0;
	} else {
		ret = 1;
	}
	put_dev_crypto(state1->d_fd);
	state1->d_fd = -1;

	if (ioctl(state2->d_fd, CIOCFSESSION, &sess2->ses) == -1) {
		ret = 0;
	} else {
		ret = 1;
	}
	put_dev_crypto(state2->d_fd);
	state2->d_fd = -1;

	return (ret);
}

void help(void)
{


}

int
main(int argc, char** argv)
{
	int i;
	int j;

	printf("### en/de crypt test !! build:%s %s\n",
		__DATE__, __TIME__);

	g_fp = fopen("./a.bin", "rb");
	if (g_fp == NULL) {
		printf("Open file failed\n");
		goto file_err;
	}

	if (argc == 2) {
		for (i = 0; i < test_algs_size; i++) {
			if (!strcmp(test_algs[i].salg_name, argv[1])) {
			/* test some algs */
				for (j =0; j < test_data_len; j++)
					single_alg_test(j, i);	
			}
		}
	}

file_err:
	if (g_fp != NULL)
		fclose(g_fp);
	return 0;
}

