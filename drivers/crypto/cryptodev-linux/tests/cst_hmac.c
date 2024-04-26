
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <crypto/cryptodev.h>
#include "testhelper.h"

#define BUF_SIZE 	16 
#define MBytes 		(1024*1024)
#define KBytes 		1024

#define ARRAY_SIZE(_x_) (sizeof(_x_) / sizeof(_x_[0]))

static unsigned char key[64] = {
	0xbb, 0xbb, 0xbb, 0xcd, 0xce, 0xcf, 0xca, 0x2b,
	0x2c, 0x2d, 0x0e, 0x0f, 0x0a, 0x0b, 0x0c, 0x0d,
	0x1a, 0x1a, 0x1a, 0x1b, 0x0b, 0x0b, 0x0c, 0x0c,
	0x1a, 0x1a, 0x1a, 0x1b, 0x0b, 0x0b, 0x0c, 0x0c,
	0xbb, 0xbb, 0xbb, 0xcd, 0xce, 0xcf, 0xca, 0x2b,
	0x2c, 0x2d, 0x0e, 0x0f, 0x0a, 0x0b, 0x0c, 0x0d,
};
static int test_data_size[] = { 
	16*16, 16*32, 1*KBytes, 2*KBytes, 4*KBytes, 8*KBytes,
	16*KBytes, 32*KBytes, 64*KBytes, 256*KBytes,
	1*MBytes, 16*MBytes, 32*MBytes, 64*MBytes, 128*MBytes
};

static int test_data_len = ARRAY_SIZE(test_data_size);
static FILE *g_fp;

#define USE_CRYPTODEV_DIGESTS	1
#define HASH_MAX_LEN	64
#define EVP_MAX_IV_LENGTH 64

#define EVP_MD_CTX_FLAG_ONESHOT 0x01

struct dev_crypto_state {
    struct session_op d_sess;
    int d_fd;
# ifdef USE_CRYPTODEV_DIGESTS
    char dummy_mac_key[HASH_MAX_LEN];
    unsigned char digest_res[HASH_MAX_LEN];
    char *mac_data;
    int mac_len;
    int block_size;
    int digest_size;
# endif
};
struct dev_crypto_ctx {
	unsigned char iv[EVP_MAX_IV_LENGTH];
	unsigned char iv_len;
	unsigned char encrypt;
	struct dev_crypto_state state;
	unsigned char flags;
};

static struct dev_crypto_ctx g_ctx;

struct test_alg {
	char *salg_name;
	int alg_type;
	int key_size;
	int block_size;
	int digest_size;
};

static struct test_alg test_algs[] = {
	/* 0 */
	{ 
		.salg_name = "hmac(md5)",
		.alg_type = CRYPTO_MD5_HMAC,
		.key_size = 16,
		.block_size = 64,
		.digest_size = 16,
	},
	/* 1 */
	{ 
		.salg_name = "hmac(sha1)",
		.alg_type = CRYPTO_SHA1_HMAC,
		.key_size = 16,
		.block_size = 64,
		.digest_size = 20,
	},
	/* 2 */
	{ 
		.salg_name = "hmac(sha224)",
		.alg_type = CRYPTO_SHA2_224_HMAC,
		.key_size = 16,
		.block_size = 64,
		.digest_size = 28,
	},
	/* 3 */
	{ 
		.alg_type = CRYPTO_SHA2_256_HMAC,
		.salg_name = "hmac(sha256)",
		.key_size = 16,
		.block_size = 64,
		.digest_size = 32,
	},
/* ############################################################ */
	/* 0 */
	/* xcbc aes 128 */
	{ 
		.salg_name = "xcbc(aes)",
		.alg_type = CRYPTO_XCBC_AES_HMAC,
		.key_size = 16,
		.block_size = 16,
		.digest_size = 16,
	},
	/* xcbc aes 192 */
	{ 
		.salg_name = "xcbc(aes)",
		.alg_type = CRYPTO_XCBC_AES_HMAC,
		.key_size = 24,
		.block_size = 16,
		.digest_size = 16,
	},
	/* xcbc aes 256 */
	{ 
		.salg_name = "xcbc(aes)",
		.alg_type = CRYPTO_XCBC_AES_HMAC,
		.key_size = 32,
		.block_size = 16,
		.digest_size = 16,
	},
	/* 1 */
	/* cmac aes 128 */
	{ 
		.alg_type = CRYPTO_CMAC_AES_HMAC,
		.salg_name = "cmac(aes)",
		.key_size = 16,
		.block_size = 16,
		.digest_size = 16,
	},
	/* cmac aes 192 */
	{ 
		.salg_name = "cmac(aes)",
		.alg_type = CRYPTO_CMAC_AES_HMAC,
		.key_size = 24,
		.block_size = 16,
		.digest_size = 16,
	},
	/* cmac aes 256 */
	{ 
		.salg_name = "cmac(aes)",
		.alg_type = CRYPTO_CMAC_AES_HMAC,
		.key_size = 32,
		.block_size = 16,
		.digest_size = 16,
	},
};
static int test_algs_size = ARRAY_SIZE(test_algs);

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

void help(void)
{


}

int print_buf(char *prompt, char *buf, int size)
{
	int i;
#define DISPLAY_LEN 32 /* 16 */
	if (prompt != NULL)
		printf("%s", prompt);
	for (i = 0; i < size; i++) {
		if ( i % DISPLAY_LEN == 0)
			printf("\n");

		/* printf("%02x ", buf[i]); */
		printf("%02x", buf[i]);
	}
	printf("\nend\n");
	return 0;
}

int timeval_subtract(char *prompt, struct timeval *x) 
{ 
	struct  timeval    tv_end;
	struct  timeval    result;
	struct  timeval    *y;

	y = &tv_end;

	gettimeofday(y, NULL);
	if (x->tv_sec > y->tv_sec ) 
		  return -1; 

	if ((x->tv_sec == y->tv_sec) && (x->tv_usec > y->tv_usec) ) 
	  return -1; 

	result.tv_sec = y->tv_sec - x->tv_sec; 
	result.tv_usec = y->tv_usec - x->tv_usec; 

	if (result.tv_usec < 0) { 
		  result.tv_sec--; 
		  result.tv_usec += 1000000; 
	} 
	if (prompt != NULL)
		printf("%s using %ld:%ld\n", prompt, result.tv_sec, result.tv_usec);
	else
		printf("using %ld:%ld\n", result.tv_sec, result.tv_usec);
	gettimeofday(x, NULL);
	return 0; 
}

static int cryptodev_digest_update(struct dev_crypto_ctx *ctx, const void *data,
		size_t count)
{
	struct crypt_op cryp;
	struct dev_crypto_state *state = &ctx->state;
	struct session_op *sess = &state->d_sess;

	if (!data || state->d_fd < 0) {
		printf("cryptodev_digest_update: illegal inputs \n");
		return (-1);
	}

	if (!count)
		return (-1);

	if (!(ctx->flags & EVP_MD_CTX_FLAG_ONESHOT)) {
		/* if application doesn't support one buffer */
		state->mac_data =
			realloc(state->mac_data, state->mac_len + count);

		if (!state->mac_data) {
			printf("cryptodev_digest_update: realloc failed\n");
			return (-1);
		}

		memcpy(state->mac_data + state->mac_len, data, count);
		state->mac_len += count;

		return (0);
	}

	memset(&cryp, 0, sizeof(cryp));

	cryp.ses = sess->ses;
	cryp.flags = COP_FLAG_NO_ZC;
	cryp.len = count;
	cryp.src = (caddr_t) data;
	cryp.dst = NULL;
	cryp.mac = (caddr_t) state->digest_res;
	if (ioctl(state->d_fd, CIOCCRYPT, &cryp) < 0) {
		printf("cryptodev_digest_update: digest failed\n");
		return (0);
	}
	return (0);
}

static int cryptodev_digest_final(struct dev_crypto_ctx *ctx, unsigned char *md)
{
	struct crypt_op cryp;
	struct dev_crypto_state *state = &ctx->state;
	struct session_op *sess = &state->d_sess;

	if (!(ctx->flags & EVP_MD_CTX_FLAG_ONESHOT)) {
		/* if application doesn't support one buffer */
		memset(&cryp, 0, sizeof(cryp));
		cryp.ses = sess->ses;
		cryp.flags = COP_FLAG_NO_ZC;
		cryp.len = state->mac_len;
		cryp.src = state->mac_data;
		cryp.dst = NULL;
		cryp.mac = (caddr_t) md;
		if (ioctl(state->d_fd, CIOCCRYPT, &cryp) < 0) {
			printf("cryptodev_digest_final: digest failed\n");
			return (-1);
		}

		return 0;
	}


	memcpy(md, state->digest_res, state->digest_size);

	return (0);
}

int single_alg_test(int len_vector, int sa_vector)
{
	int	ret = -1;
	int	z;
	int	single_size;
	int	real_size;
	int	digest_size;
	int	passed_cnt;
	int	total;

        int     key_size;

	char *in;
	char *out;
	/* init */
	struct dev_crypto_state *state = &g_ctx.state;
	struct session_op *sess = &state->d_sess;
	int alg_type;

	struct  timeval    tv_begin;

	digest_size = test_algs[sa_vector].digest_size;
	alg_type = test_algs[sa_vector].alg_type;
	key_size = test_algs[sa_vector].key_size;
	total = test_data_size[len_vector];
	in  = malloc(total);
	out = malloc(digest_size);

	if (in == NULL || out == NULL) {
		printf("malloc error");
		goto ext;
	}

	passed_cnt = 0;

	memset(&g_ctx, 0, sizeof(struct dev_crypto_ctx));
	memset(state, 0, sizeof(struct dev_crypto_state)); 
	memset(sess, 0, sizeof(struct session_op));

	if ((state->d_fd = get_dev_crypto()) < 0) { 
		printf("cryptodev_digest_init: Can't get Dev \n"); 
		return (-1); 
	}    
	state->digest_size = digest_size;

	memcpy(state->dummy_mac_key, key, key_size);

	sess->mackey = state->dummy_mac_key;
	sess->mackeylen = key_size;
	sess->mac = alg_type;
	

	/* digest init */
	if (ioctl(state->d_fd, CIOCGSESSION, sess) < 0) { 
		put_dev_crypto(state->d_fd);
		state->d_fd = -1;
		printf("cryptodev_digest_init: Open session failed\n");
		return (-1); 
	}    

	printf(" ######## name:%s ########## key_size:%d\n",
		test_algs[sa_vector].salg_name, key_size);

	g_ctx.flags = EVP_MD_CTX_FLAG_ONESHOT; 

	printf("block_size:%d total:%d\n", test_algs[sa_vector].block_size, total);
	fseek(g_fp, 0L, SEEK_SET);

	gettimeofday(&tv_begin, NULL);


	for (z = 0; z < total; ) {
		single_size = total - passed_cnt;

		if ((real_size = fread(in + passed_cnt, 1, single_size, g_fp) != single_size)) {
			printf("real_size %d, single_size:%d\n", real_size, single_size);
			printf("Read file failed\n");
			goto ext;
		}

		/* digest update */
		cryptodev_digest_update(&g_ctx, in + passed_cnt, single_size);
		
		
		passed_cnt += single_size;
		z += passed_cnt;
	}
	/* digest final */
	cryptodev_digest_final(&g_ctx, (unsigned char *)out);

	timeval_subtract(NULL, &tv_begin);

	print_buf("digest:", out, digest_size);

	ret = 0;
ext:
	if (in != NULL)
		free(in);
	if (out != NULL)
		free(out);

	/* clean up */
	if (state->mac_data) {
		free(state->mac_data);
		state->mac_data = NULL;
		state->mac_len = 0;
	}

	if (ioctl(state->d_fd, CIOCFSESSION, &sess->ses) == -1) {
		printf("close session error\r\n");
		ret = -1;
	} else {
		ret = 0;
	}	

	put_dev_crypto(state->d_fd);
	state->d_fd = -1;
			
	return ret;
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
