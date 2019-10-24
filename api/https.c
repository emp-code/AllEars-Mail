#define _GNU_SOURCE // for memmem, strcasestr

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h> // for islower

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include "https.h"

#include "https_post.h"

#define AEM_MINLEN_POST 76 // POST /api/account/browse HTTP/1.1\r\nHost: a.bc:7850\r\nContent-Length: 8264\r\n\r\n
#define AEM_MAXLEN_REQ 800
#define AEM_HTTPS_TIMEOUT 30

#define AEM_SKIP_URL_POST 10 // 'POST /api/'

#define AEM_HTTPS_REQUEST_INVALID -1
#define AEM_HTTPS_REQUEST_POST 1
#define AEM_HTTPS_REQUEST_PUBKEY 50

static const int https_ciphersuites[] = {
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8,
	MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CCM,
	MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CCM_8,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_DHE_RSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8,
	MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CCM,
	MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CCM_8,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS_DHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
0};

static const mbedtls_ecp_group_id https_curves[] = {
	MBEDTLS_ECP_DP_CURVE448,
	MBEDTLS_ECP_DP_CURVE25519,
	MBEDTLS_ECP_DP_SECP521R1,
	MBEDTLS_ECP_DP_SECP384R1,
MBEDTLS_ECP_DP_NONE};

static const int https_hashes[] = {
	MBEDTLS_SSL_HASH_SHA512,
MBEDTLS_MD_NONE};

__attribute__((warn_unused_result))
static int getRequestType(char * const req, size_t lenReq, const char * const domain, const size_t lenDomain) {
	if (memcmp(req, "GET /api/pubkey", 15) == 0) return AEM_HTTPS_REQUEST_PUBKEY;

	if (lenReq < AEM_MINLEN_POST) return AEM_HTTPS_REQUEST_INVALID;

	// First line
	if (memcmp(req, "POST /api/", 10) != 0) return AEM_HTTPS_REQUEST_INVALID;
	for (int i = 10; i < 17; i++) {if (!islower(req[i])) return AEM_HTTPS_REQUEST_INVALID;}
	if (req[17] != '/') return AEM_HTTPS_REQUEST_INVALID;
	for (int i = 18; i < 24; i++) {if (!islower(req[i])) return AEM_HTTPS_REQUEST_INVALID;}
	if (memcmp(req + 24, " HTTP/1.1\r\n", 11) != 0) return AEM_HTTPS_REQUEST_INVALID;

	char * const reqEnd = memmem(req, lenReq, "\r\n\r\n", 4);
	if (reqEnd == NULL) return AEM_HTTPS_REQUEST_INVALID;

	lenReq = reqEnd - req + 2; // Include \r\n at end
	if (lenReq > AEM_MAXLEN_REQ) return AEM_HTTPS_REQUEST_INVALID;
	if (memchr(req, '\0', lenReq) != NULL) return AEM_HTTPS_REQUEST_INVALID;
	reqEnd[2] = '\0';

	// Host header
	const char * const host = strstr(req, "\r\nHost: ");
	if (host == NULL) return AEM_HTTPS_REQUEST_INVALID;
	if (strncmp(host + 8, domain, lenDomain) != 0) return AEM_HTTPS_REQUEST_INVALID;
	if (strncmp(host + 8 + lenDomain, ":7850\r\n", 7) != 0) return AEM_HTTPS_REQUEST_INVALID;

	// Forbidden request headers
	if (
		   (strcasestr(req, "\r\nAuthorization:")       != NULL)
		|| (strcasestr(req, "\r\nCookie:")              != NULL)
		|| (strcasestr(req, "\r\nExpect:")              != NULL)
		|| (strcasestr(req, "\r\nHTTP2-Settings:")      != NULL)
		|| (strcasestr(req, "\r\nIf-Match:")            != NULL)
		|| (strcasestr(req, "\r\nIf-Modified-Since:")   != NULL)
		|| (strcasestr(req, "\r\nIf-None-Match:")       != NULL)
		|| (strcasestr(req, "\r\nIf-Range:")            != NULL)
		|| (strcasestr(req, "\r\nIf-Unmodified-Since:") != NULL)
		|| (strcasestr(req, "\r\nRange:")               != NULL)
		|| (strcasestr(req, "\r\nSec-Fetch-Site: none") != NULL)
		|| (strcasestr(req, "\r\nSec-Fetch-Site: same-origin") != NULL)
		// These are only for preflighted requests, which All-Ears doesn't use
		|| (strcasestr(req, "\r\nAccess-Control-Request-Method:")  != NULL)
		|| (strcasestr(req, "\r\nAccess-Control-Request-Headers:") != NULL)
	) return AEM_HTTPS_REQUEST_INVALID;

	if (strstr(req, "\r\nContent-Length: 8264\r\n") == NULL) return AEM_HTTPS_REQUEST_INVALID;

	reqEnd[2] = '\r';
	return AEM_HTTPS_REQUEST_POST;
}

void handlePost(mbedtls_ssl_context * const ssl, char * const buf, const size_t lenReq, const unsigned char * const ssk, const unsigned char * const addrKey) {
	const char * const urlEnd = strchr(buf + AEM_SKIP_URL_POST, ' ');
	if (urlEnd == NULL) return;
	const size_t lenUrl = urlEnd - (buf + AEM_SKIP_URL_POST);

	char url[lenUrl];
	memcpy(url, buf + AEM_SKIP_URL_POST, lenUrl);

	const char *post = strstr(buf + AEM_MINLEN_POST - 4, "\r\n\r\n");
	if (post == NULL) return;
	post += 4;

	size_t lenPost = lenReq - (post - buf);
	if (lenPost > 0) memmove(buf, post, lenPost);

	while (lenPost < AEM_HTTPS_POST_BOXED_SIZE) {
		int ret;
		do {ret = mbedtls_ssl_read(ssl, (unsigned char*)(buf + lenPost), AEM_HTTPS_POST_BOXED_SIZE - lenPost);} while (ret == MBEDTLS_ERR_SSL_WANT_READ);
		if (ret < 0) return;
		lenPost += ret;
	}

	https_post(ssl, ssk, addrKey, url, (unsigned char*)buf);
}

static void tlsFree(mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf, mbedtls_entropy_context *entropy, mbedtls_ctr_drbg_context *ctr_drbg) {
	mbedtls_ssl_free(ssl);
	mbedtls_ssl_config_free(conf);
	mbedtls_entropy_free(entropy);
	mbedtls_ctr_drbg_free(ctr_drbg);
}

void respond_https(int sock, mbedtls_x509_crt * const srvcert, mbedtls_pk_context * const pkey, const unsigned char * const ssk, const unsigned char * const addrKey, const char * const domain, const size_t lenDomain) {
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;

	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	int ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {
		printf("[HTTPS] mbedtls_ssl_config_defaults returned %d\n", ret);
		tlsFree(&ssl, &conf, &entropy, &ctr_drbg);
		return;
	}

	mbedtls_ssl_conf_ca_chain(&conf, srvcert->next, NULL);
	mbedtls_ssl_conf_ciphersuites(&conf, https_ciphersuites);
	mbedtls_ssl_conf_curves(&conf, https_curves);
	mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3); // Require TLS v1.2+
	mbedtls_ssl_conf_read_timeout(&conf, AEM_HTTPS_TIMEOUT);
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
	mbedtls_ssl_conf_sig_hashes(&conf, https_hashes);

	ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
	if (ret != 0) {
		printf("[HTTPS] mbedtls_ctr_drbg_seed returned %d\n", ret);
		tlsFree(&ssl, &conf, &entropy, &ctr_drbg);
		return;
	}

	ret = mbedtls_ssl_conf_own_cert(&conf, srvcert, pkey);
	if (ret != 0) {
		printf("[HTTPS] mbedtls_ssl_conf_own_cert returned %d\n", ret);
		tlsFree(&ssl, &conf, &entropy, &ctr_drbg);
		return;
	}

	ret = mbedtls_ssl_setup(&ssl, &conf);
	if (ret != 0) {
		printf("[HTTPS] mbedtls_ssl_setup returned %d\n", ret);
		tlsFree(&ssl, &conf, &entropy, &ctr_drbg);
		return;
	}

	mbedtls_ssl_set_bio(&ssl, &sock, mbedtls_net_send, mbedtls_net_recv, NULL);

	// Handshake
	while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			printf("[HTTPS] mbedtls_ssl_handshake returned %d\n", ret);
			tlsFree(&ssl, &conf, &entropy, &ctr_drbg);
			return;
		}
	}

	unsigned char * const req = malloc(AEM_HTTPS_POST_BOXED_SIZE + 1);

	int lenReq;
	do {lenReq = mbedtls_ssl_read(&ssl, req, AEM_HTTPS_POST_BOXED_SIZE);} while (lenReq == MBEDTLS_ERR_SSL_WANT_READ);

	if (lenReq > 0) {
		req[lenReq] = '\0';
		switch (getRequestType((char*)req, lenReq, domain, lenDomain)) {
			case AEM_HTTPS_REQUEST_PUBKEY: https_pubkey(&ssl, ssk); break;
			case AEM_HTTPS_REQUEST_POST: {
				handlePost(&ssl, (char*)req, lenReq, ssk, addrKey);
				sodium_memzero(req, AEM_HTTPS_POST_BOXED_SIZE);
			}
		}
	}

	free(req);
	tlsFree(&ssl, &conf, &entropy, &ctr_drbg);
}
