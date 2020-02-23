#define _GNU_SOURCE // for memmem, strcasestr

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h> // for islower
#include <syslog.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include "../Global.h"

#include "https.h"
#include "post.h"

#define AEM_MINLEN_POST 75 // POST /api/account/browse HTTP/1.1\r\nHost: a.bc:302\r\nContent-Length: 8264\r\n\r\n
#define AEM_MAXLEN_REQ 800
#define AEM_HTTPS_TIMEOUT 30

#define AEM_SKIP_URL_POST 10 // 'POST /api/'
#define AEM_LEN_URL_POST 14 // 'account/browse'

#define AEM_HTTPS_REQUEST_INVALID -1
#define AEM_HTTPS_REQUEST_POST 1
#define AEM_HTTPS_REQUEST_PUBKEY 50

static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

static const int https_ciphersuites[] = {
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
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

static char domain[AEM_MAXLEN_DOMAIN];
static size_t lenDomain;

static unsigned char req[AEM_HTTPS_POST_BOXED_SIZE + 1];

int setDomain(const char * const newDomain, const size_t len) {
	if (len > AEM_MAXLEN_DOMAIN) return -1;

	lenDomain = len;
	memcpy(domain, newDomain, len);
	return 0;
}

__attribute__((warn_unused_result))
static int handleRequest(size_t lenReq) {
	if (memcmp(req, "GET /api/pubkey HTTP/1.1\r\n", 26) == 0) return AEM_HTTPS_REQUEST_PUBKEY;

	if (lenReq < AEM_MINLEN_POST) return AEM_HTTPS_REQUEST_INVALID;

	// First line
	if (memcmp(req, "POST /api/", 10) != 0) return AEM_HTTPS_REQUEST_INVALID;
	for (int i = 10; i < 17; i++) {if (!islower(req[i])) return AEM_HTTPS_REQUEST_INVALID;}
	if (req[17] != '/') return AEM_HTTPS_REQUEST_INVALID;
	for (int i = 18; i < 24; i++) {if (!islower(req[i])) return AEM_HTTPS_REQUEST_INVALID;}
	if (memcmp(req + 24, " HTTP/1.1\r\n", 11) != 0) return AEM_HTTPS_REQUEST_INVALID;

	const unsigned char * const reqEnd = memmem(req, AEM_MAXLEN_REQ, "\r\n\r\n", 4);
	if (reqEnd == NULL) return AEM_HTTPS_REQUEST_INVALID;

	lenReq = reqEnd - req + 2; // Include \r\n at end
	if (memchr(req, '\0', lenReq) != NULL) return AEM_HTTPS_REQUEST_INVALID;

	// Host header
	const unsigned char * const host = memmem(req, lenReq, "\r\nHost: ", 8);
	if (host == NULL) return AEM_HTTPS_REQUEST_INVALID;
	if (memcmp(host + 8, domain, lenDomain) != 0) return AEM_HTTPS_REQUEST_INVALID;
	if (memcmp(host + 8 + lenDomain, ":302\r\n", 6) != 0) return AEM_HTTPS_REQUEST_INVALID;

	if (memmem(req, lenReq, "\r\nContent-Length: 8266\r\n", 24) == NULL) return AEM_HTTPS_REQUEST_INVALID;

	// Forbidden request headers
	const char * const creq = (char*)req;
	if (
		   (strcasestr(creq, "\r\nAuthorization:")       != NULL)
		|| (strcasestr(creq, "\r\nCookie:")              != NULL)
		|| (strcasestr(creq, "\r\nExpect:")              != NULL)
		|| (strcasestr(creq, "\r\nHTTP2-Settings:")      != NULL)
		|| (strcasestr(creq, "\r\nIf-Match:")            != NULL)
		|| (strcasestr(creq, "\r\nIf-Modified-Since:")   != NULL)
		|| (strcasestr(creq, "\r\nIf-None-Match:")       != NULL)
		|| (strcasestr(creq, "\r\nIf-Range:")            != NULL)
		|| (strcasestr(creq, "\r\nIf-Unmodified-Since:") != NULL)
		|| (strcasestr(creq, "\r\nRange:")               != NULL)
		|| (strcasestr(creq, "\r\nSec-Fetch-Site: none") != NULL)
		|| (strcasestr(creq, "\r\nSec-Fetch-Site: same-origin") != NULL)
		// These are only for preflighted requests, which All-Ears doesn't use
		|| (strcasestr(creq, "\r\nAccess-Control-Request-Method:")  != NULL)
		|| (strcasestr(creq, "\r\nAccess-Control-Request-Headers:") != NULL)
	) return AEM_HTTPS_REQUEST_INVALID;

	const char *secDest = strcasestr(creq, "\r\nSec-Fetch-Dest: ");
	if (secDest != NULL) {
		secDest += 18;
		if (
		   strncasecmp(secDest, "audio", 5) == 0
		|| strncasecmp(secDest, "audioworklet", 12) == 0
		|| strncasecmp(secDest, "document", 8) == 0
		|| strncasecmp(secDest, "embed", 5) == 0
		|| strncasecmp(secDest, "font", 4) == 0
		|| strncasecmp(secDest, "image", 5) == 0
		|| strncasecmp(secDest, "manifest", 8) == 0
		|| strncasecmp(secDest, "nested-document", 15) == 0
		|| strncasecmp(secDest, "object", 6) == 0
		|| strncasecmp(secDest, "paintworklet", 12) == 0
		|| strncasecmp(secDest, "report", 6) == 0
		|| strncasecmp(secDest, "script", 6) == 0
		|| strncasecmp(secDest, "serviceworker", 13) == 0
		|| strncasecmp(secDest, "sharedworker", 12) == 0
		|| strncasecmp(secDest, "style", 5) == 0
		|| strncasecmp(secDest, "track", 5) == 0
		|| strncasecmp(secDest, "video", 5) == 0
		|| strncasecmp(secDest, "worker", 6) == 0
		|| strncasecmp(secDest, "xslt", 4) == 0
		) return AEM_HTTPS_REQUEST_INVALID;
	}

	return AEM_HTTPS_REQUEST_POST;
}

void handlePost(mbedtls_ssl_context * const ssl, const size_t lenReq) {
	char url[AEM_LEN_URL_POST];
	memcpy(url, req + AEM_SKIP_URL_POST, AEM_LEN_URL_POST);

	const unsigned char *post = memmem(req, lenReq, "\r\n\r\n", 4);
	if (post == NULL) return;
	post += 4;

	size_t lenPost = lenReq - (post - req);

	if (lenPost > 0) {
		const size_t lenDel = (lenReq - lenPost);
		memcpy(req, req + lenDel, lenDel);
		memcpy(req + lenDel, req + lenDel * 2, lenPost - lenDel);
	}

	while (lenPost < AEM_HTTPS_POST_BOXED_SIZE) {
		int ret;
		do {ret = mbedtls_ssl_read(ssl, req + lenPost, AEM_HTTPS_POST_BOXED_SIZE - lenPost);} while (ret == MBEDTLS_ERR_SSL_WANT_READ);
		if (ret < 1) return;
		lenPost += ret;
	}

	https_post(ssl, url, req);
}

void tlsFree(void) {
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
	mbedtls_entropy_free(&entropy);
	mbedtls_ctr_drbg_free(&ctr_drbg);
}

int tlsSetup(mbedtls_x509_crt * const tlsCert, mbedtls_pk_context * const tlsKey) {
	explicit_bzero(req, AEM_HTTPS_POST_BOXED_SIZE);

	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	int ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret != 0) {syslog(LOG_MAIL | LOG_NOTICE, "mbedtls_ssl_config_defaults failed: %d", ret); return -1;}

	mbedtls_ssl_conf_ca_chain(&conf, tlsCert->next, NULL);
	mbedtls_ssl_conf_ciphersuites(&conf, https_ciphersuites);
	mbedtls_ssl_conf_curves(&conf, https_curves);
	mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3); // Require TLS v1.2+
	mbedtls_ssl_conf_read_timeout(&conf, AEM_HTTPS_TIMEOUT);
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
	mbedtls_ssl_conf_sig_hashes(&conf, https_hashes);

	ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
	if (ret != 0) {syslog(LOG_MAIL | LOG_NOTICE, "mbedtls_ctr_drbg_seed failed: %d", ret); return -1;}

	ret = mbedtls_ssl_conf_own_cert(&conf, tlsCert, tlsKey);
	if (ret != 0) {syslog(LOG_MAIL | LOG_NOTICE, "mbedtls_ssl_conf_own_cert failed: %d", ret); return -1;}

	ret = mbedtls_ssl_setup(&ssl, &conf);
	if (ret != 0) {syslog(LOG_MAIL | LOG_NOTICE, "mbedtls_ssl_setup failed: %d", ret); return -1;}

	return 0;
}

void respond_https(int sock) {
	mbedtls_ssl_set_bio(&ssl, &sock, mbedtls_net_send, mbedtls_net_recv, NULL);

	int ret;
	while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
			syslog(LOG_MAIL | LOG_NOTICE, "mbedtls_ssl_handshake failed: %d", ret);
			mbedtls_ssl_close_notify(&ssl);
			mbedtls_ssl_session_reset(&ssl);
			return;
		}
	}

	do {ret = mbedtls_ssl_read(&ssl, req, AEM_HTTPS_POST_BOXED_SIZE);} while (ret == MBEDTLS_ERR_SSL_WANT_READ);

	if (ret > 0) {
		req[ret] = '\0';
		switch (handleRequest(ret)) {
			case AEM_HTTPS_REQUEST_PUBKEY: https_pubkey(&ssl); break;
			case AEM_HTTPS_REQUEST_POST: handlePost(&ssl, ret); break;
		}
	}

	explicit_bzero(req, AEM_HTTPS_POST_BOXED_SIZE);
	mbedtls_ssl_close_notify(&ssl);
	mbedtls_ssl_session_reset(&ssl);
}
