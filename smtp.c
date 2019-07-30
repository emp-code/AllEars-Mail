#define AEM_SMTP_SIZE_CMD 512 // RFC5321: min. 512

#define AEM_SMTP_MAX_ADDRSIZE 50 // Should be 37+lenDomain
#define AEM_SMTP_MAX_ADDRSIZE_TO 5000 // RFC5321: must accept 100 recipients at minimum
#define AEM_SMTP_TIMEOUT 30

#define AEM_CIPHERSUITES_SMTP {\
MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,\
MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,\
MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,\
MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,\
MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,\
MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256}

#define AEM_SMTP_SIZE_BODY 65536 // RFC5321: min. 64k; if changed, set the HLO responses and their lengths below also

#define AEM_EHLO_RESPONSE_LEN 32
#define AEM_EHLO_RESPONSE \
"\r\n250-SIZE 65536" \
"\r\n250 STARTTLS" \
"\r\n"

#define AEM_SHLO_RESPONSE_LEN 18
#define AEM_SHLO_RESPONSE \
"\r\n250 SIZE 65536" \
"\r\n"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sodium.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

#include "Includes/SixBit.h"
#include "Database.h"
#include "Message.h"

#include "smtp.h"

static int recv_aem(const int sock, mbedtls_ssl_context *ssl, char *buf, const size_t maxSize) {
	if (ssl == NULL && sock < 1) return -1;

	if (ssl == NULL) return recv(sock, buf, maxSize, 0);

	int ret;
	do {ret = mbedtls_ssl_read(ssl, (unsigned char*)buf, maxSize);} while (ret == MBEDTLS_ERR_SSL_WANT_READ);
	return ret;
}

static int send_aem(const int sock, mbedtls_ssl_context* ssl, const char * const data, const size_t lenData) {
	if (ssl == NULL && sock > 0) return send(sock, data, lenData, 0);

	if (ssl == NULL) return -1;

	size_t sent = 0;
	while (sent < lenData) {
		int ret;
		do {ret = mbedtls_ssl_write(ssl, (unsigned char*)(data + sent), lenData - sent);} while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);

		if (ret < 0) return ret;

		sent += ret;
	}

	return sent;
}

static size_t smtp_addr(const size_t len, const char * const buf, char addr[AEM_SMTP_MAX_ADDRSIZE]) {
	size_t start = 1;
	size_t lenAddr = len - 1;

	while (lenAddr > 0 && buf[start - 1] != '<') {start++; lenAddr--;}
	if (lenAddr < 1) return 0;

	while (lenAddr > 0 && buf[start + lenAddr] != '>') lenAddr--;
	if (lenAddr < 1) return 0;

	if (lenAddr > AEM_SMTP_MAX_ADDRSIZE) return 0;
	memcpy(addr, buf + start, lenAddr);
	return lenAddr;
}

static bool smtp_greet(const int sock, const char *domain, const size_t lenDomain) {
	const int lenGreet = 12 + lenDomain;
	char ourGreeting[lenGreet];
	memcpy(ourGreeting, "220 ", 4);
	memcpy(ourGreeting + 4, domain, lenDomain);
	memcpy(ourGreeting + 4 + lenDomain, " ESMTP\r\n", 8);
	return (send(sock, ourGreeting, lenGreet, 0) == lenGreet);
}

static bool smtp_shlo(mbedtls_ssl_context *tls, const char *domain, const size_t lenDomain) {
	const ssize_t lenShlo = 4 + lenDomain + AEM_SHLO_RESPONSE_LEN;
	char shlo[lenShlo];
	memcpy(shlo, "250-", 4);
	memcpy(shlo + 4, domain, lenDomain);
	memcpy(shlo + 4 + lenDomain, AEM_SHLO_RESPONSE, AEM_SHLO_RESPONSE_LEN);
	return (send_aem(0, tls, shlo, lenShlo) == lenShlo);
}

static bool smtp_helo(const int sock, const char *domain, const size_t lenDomain, const ssize_t bytes, const char *buf) {
	if (bytes < 4) return false;

	if (strncasecmp(buf, "EHLO", 4) == 0) {
		const ssize_t lenHelo = 4 + lenDomain + AEM_EHLO_RESPONSE_LEN;
		char helo[lenHelo];
		memcpy(helo, "250-", 4);
		memcpy(helo + 4, domain, lenDomain);
		memcpy(helo + 4 + lenDomain, AEM_EHLO_RESPONSE, AEM_EHLO_RESPONSE_LEN);
		return (send(sock, helo, lenHelo, 0) == lenHelo);
	} else if (strncasecmp(buf, "HELO", 4) == 0) {
		const ssize_t lenHelo = 6 + lenDomain;
		char helo[lenHelo];
		memcpy(helo, "250 ", 4);
		memcpy(helo + 4, domain, lenDomain);
		memcpy(helo + 4 + lenDomain, "\r\n", 2);
		return (send(sock, helo, lenHelo, 0) == lenHelo);
	}

	return false;
}

static void smtp_fail(const int sock, mbedtls_ssl_context *tls, const unsigned long ip, const int code) {
	send_aem(sock, tls, "421 Bye\r\n", 9);
	close(sock);

	if (ip == 0) return;
	struct in_addr ip_addr; ip_addr.s_addr = ip;
	printf("[SMTP] Error receiving message (Code: %d, IP: %s)\n", code, inet_ntoa(ip_addr));
}

static void tlsFree(mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf, mbedtls_ctr_drbg_context *ctr_drbg, mbedtls_entropy_context *entropy) {
	if (ssl == NULL) return;
	mbedtls_entropy_free(entropy);
	mbedtls_ctr_drbg_free(ctr_drbg);
	mbedtls_ssl_config_free(conf);
	mbedtls_ssl_free(ssl);
}

static void deliverMessage(const char *to, const size_t lenTo, const char *from, const size_t lenFrom, const char *msgBody, const size_t lenMsgBody, const uint32_t clientIp, const int cs) {
	unsigned char *binTo = textToSixBit(to, lenTo, 18);
	if (binTo == NULL) {puts("[SMTP] Failed to deliver email: textToSixBit failed"); return;}

	unsigned char pk[crypto_box_PUBLICKEYBYTES];
	int ret = getPublicKeyFromAddress(binTo, pk, (unsigned char*)"TestTestTestTest");
	if (ret != 0) {free(binTo); puts("[SMTP] Discarding email sent to nonexistent address"); return;}

	size_t bodyLen = lenMsgBody;
	unsigned char* boxSet = makeMsg_Ext(pk, binTo, clientIp, cs, msgBody, &bodyLen);
	const size_t bsLen = AEM_HEADBOX_SIZE + crypto_box_SEALBYTES + bodyLen + crypto_box_SEALBYTES;
	if (boxSet == NULL) {free(binTo); puts("[SMTP]: Failed to deliver email: makeMsg_Ext failed"); return;}

	int64_t upk64;
	memcpy(&upk64, pk, 8);
	ret = addUserMessage(upk64, boxSet, bsLen);
	free(boxSet);
	free(binTo);

	if (ret != 0) puts("[SMTP] Failed to deliver email: addUserMessage failed");
}

static bool isValidAemAddress(const char *c, const size_t len) {
	for (size_t i = 0; i < len; i++) {
		if (!isalnum(c[i]) && c[i] != '.' && c[i] != '-') return false;
	}

	return true;
}

static bool addressIsOurs(const char *addr, const size_t lenAddr, const char *domain, const size_t lenDomain) {
	return (
	   lenAddr < AEM_SMTP_MAX_ADDRSIZE
	&& lenAddr > (lenDomain + 1)
	&& addr[lenAddr - lenDomain - 1] == '@'
	&& strncasecmp(addr + lenAddr - lenDomain, domain, lenDomain) == 0
	&& isValidAemAddress(addr, lenAddr - lenDomain - 1)
	);
}

void respond_smtp(int sock, mbedtls_x509_crt *srvcert, mbedtls_pk_context *pkey, const uint32_t clientIp, const unsigned char seed[16], const char *domain, const size_t lenDomain) {
	if (!smtp_greet(sock, domain, lenDomain)) return smtp_fail(sock, NULL, clientIp, 0);

	char buf[AEM_SMTP_SIZE_CMD];
	int bytes = recv(sock, buf, AEM_SMTP_SIZE_CMD, 0);

	const size_t lenGreeting = bytes - 7;
	char greeting[lenGreeting];
	memcpy(greeting, buf + 5, lenGreeting);

	if (!smtp_helo(sock, domain, lenDomain, bytes, buf)) return smtp_fail(sock, NULL, clientIp, 1);

	bytes = recv(sock, buf, AEM_SMTP_SIZE_CMD, 0);

	mbedtls_ssl_context *tls = NULL;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_entropy_context entropy;

	if (bytes >= 8 && strncasecmp(buf, "STARTTLS", 8) == 0) {
		send(sock, "220 Ok\r\n", 8, 0);
		tls = &ssl;

		mbedtls_ssl_config_init(&conf);

		int ret;
		if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
			printf("[SMTP] Terminating: mbedtls_ssl_config_defaults returned %d\n", ret);
			mbedtls_ssl_config_free(&conf);
			mbedtls_ssl_free(&ssl);
			return;
		}

		mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3); // Require TLS v1.2+
		mbedtls_ssl_conf_read_timeout(&conf, AEM_SMTP_TIMEOUT);
		const int cs[] = AEM_CIPHERSUITES_SMTP;
		mbedtls_ssl_conf_ciphersuites(&conf, cs);

		mbedtls_ctr_drbg_init(&ctr_drbg);
		mbedtls_entropy_init(&entropy);
		if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, seed, 16)) != 0) {
			printf("[SMTP] Terminating: mbedtls_ctr_drbg_seed returned %d\n", ret);
			tlsFree(tls, &conf, &ctr_drbg, &entropy);
			return;
		}

		mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

		mbedtls_ssl_conf_ca_chain(&conf, srvcert->next, NULL);
		if ((ret = mbedtls_ssl_conf_own_cert(&conf, srvcert, pkey)) != 0) {
			printf("[SMTP] Terminating: mbedtls_ssl_conf_own_cert returned %d\n", ret);
			tlsFree(tls, &conf, &ctr_drbg, &entropy);
			return;
		}

		mbedtls_ssl_init(tls);

		if ((ret = mbedtls_ssl_setup(tls, &conf)) != 0) {
			printf("[SMTP] Terminating: mbedtls_ssl_setup returned %d\n", ret);
			tlsFree(tls, &conf, &ctr_drbg, &entropy);
			return;
		}

		mbedtls_ssl_set_bio(tls, &sock, mbedtls_net_send, mbedtls_net_recv, NULL);

		// Handshake
		while ((ret = mbedtls_ssl_handshake(tls)) != 0) {
			if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
				char error_buf[100];
				mbedtls_strerror(ret, error_buf, 100);
				printf("[SMTP] Terminating: mbedtls_ssl_handshake returned %d (%s)\n", ret, error_buf);
				tlsFree(tls, &conf, &ctr_drbg, &entropy);
				return;
			}
		}

		bytes = recv_aem(0, tls, buf, AEM_SMTP_SIZE_CMD);
		if (bytes == 0) {
			puts("[SMTP] Terminating: Client closed connection after StartTLS");
			tlsFree(tls, &conf, &ctr_drbg, &entropy);
			return;
		} else if (bytes < 4 || (strncasecmp(buf, "EHLO", 4) != 0 && strncasecmp(buf, "HELO", 4) != 0)) {
			printf("[SMTP] Terminating: Expected EHLO/HELO after StartTLS, but received: %.*s\n", bytes, buf);
			tlsFree(tls, &conf, &ctr_drbg, &entropy);
			return;
		}
		smtp_shlo(tls, domain, lenDomain);

		bytes = recv_aem(0, tls, buf, AEM_SMTP_SIZE_CMD);
	}

	size_t lenFrom = 0, lenTo = 0;
	char from[AEM_SMTP_MAX_ADDRSIZE];
	char to[AEM_SMTP_MAX_ADDRSIZE_TO];

	char *body = NULL;
	size_t lenBody = 0;

	while(1) {
		if (bytes < 4) {
			struct in_addr ip_addr; ip_addr.s_addr = clientIp;
			if (bytes == 0) printf("[SMTP] Terminating: client closed connection (IP: %s; greeting: %.*s)\n", inet_ntoa(ip_addr), (int)lenGreeting, greeting);
			else printf("[SMTP] Terminating: only received %d bytes (IP: %s; greeting: %.*s)\n", bytes, inet_ntoa(ip_addr), (int)lenGreeting, greeting);
			break;
		}

		if (bytes > 10 && strncasecmp(buf, "MAIL FROM:", 10) == 0) {
			lenFrom = smtp_addr(bytes - 10, buf + 10, from);
			if (lenFrom < 1) {
				tlsFree(tls, &conf, &ctr_drbg, &entropy);
				return smtp_fail(sock, tls, clientIp, 100);
			}
		}

		else if (bytes > 8 && strncasecmp(buf, "RCPT TO:", 8) == 0) {
			if (lenFrom < 1) {
				if (send_aem(sock, tls, "503 Ok\r\n", 8) != 8) {
					tlsFree(tls, &conf, &ctr_drbg, &entropy);
					return smtp_fail(sock, tls, clientIp, 101);
				}

				continue;
			}

			char newTo[AEM_SMTP_MAX_ADDRSIZE];
			size_t lenNewTo = smtp_addr(bytes - 8, buf + 8, newTo);
			if (lenNewTo < 1) {
				tlsFree(tls, &conf, &ctr_drbg, &entropy);
				return smtp_fail(sock, tls, clientIp, 102);
			}

			if (!addressIsOurs(newTo, lenNewTo, domain, lenDomain)) {
				if (send_aem(sock, tls, "550 Ok\r\n", 8) != 8) {
					tlsFree(tls, &conf, &ctr_drbg, &entropy);
					return smtp_fail(sock, tls, clientIp, 103);
				}

				continue;
			}

			if ((lenTo + 1 + lenNewTo) > AEM_SMTP_MAX_ADDRSIZE_TO) {
				if (send_aem(sock, tls, "452 Ok\r\n", 8) != 8) { // Too many recipients
					tlsFree(tls, &conf, &ctr_drbg, &entropy);
					return smtp_fail(sock, tls, clientIp, 104);
				}

				continue;
			}

			if (lenTo > 0) {
				to[lenTo] = '\n';
				lenTo++;
			}

			memcpy(to + lenTo, newTo, lenNewTo);
			lenTo += lenNewTo;
		}

		else if (strncasecmp(buf, "RSET", 4) == 0) {
			lenFrom = 0;
			lenTo = 0;
		}

		else if (strncasecmp(buf, "VRFY", 4) == 0) {
			if (send_aem(sock, tls, "252 Ok\r\n", 8) != 8) { // 252 = Cannot VRFY user, but will accept message and attempt delivery
				tlsFree(tls, &conf, &ctr_drbg, &entropy);
				return smtp_fail(sock, tls, clientIp, 105);
			}

			continue;
		}

		else if (strncasecmp(buf, "QUIT", 4) == 0) {
			send_aem(sock, tls, "221 Ok\r\n", 8);
			break;
		}

		else if (strncasecmp(buf, "DATA", 4) == 0) {
			if (lenFrom < 1 || lenTo < 1) {
				if (send_aem(sock, tls, "503 Ok\r\n", 8) != 8) {
					tlsFree(tls, &conf, &ctr_drbg, &entropy);
					return smtp_fail(sock, tls, clientIp, 106);
				}

				continue;
			}

			if (send_aem(sock, tls, "354 Ok\r\n", 8) != 8) {
				tlsFree(tls, &conf, &ctr_drbg, &entropy);
				return smtp_fail(sock, tls, clientIp, 107);
			}

			body = malloc(AEM_SMTP_SIZE_BODY);

			while(1) {
				bytes = recv_aem(sock, tls, body + lenBody, AEM_SMTP_SIZE_BODY - lenBody);
				if (bytes < 1) break;

				lenBody += bytes;

				if (lenBody >= AEM_SMTP_SIZE_BODY) {bytes = 0; break;}
				if (lenBody > 5 && memcmp(body + lenBody - 5, "\r\n.\r\n", 5) == 0) break;
			}

			const int cs = (tls == NULL) ? 0 : mbedtls_ssl_get_ciphersuite_id(mbedtls_ssl_get_ciphersuite(tls));
			deliverMessage(to, lenTo - lenDomain - 1, from, lenFrom, body, lenBody, clientIp, cs);

			lenFrom = 0;
			lenTo = 0;
			lenBody = 0;
			free(body);

			if (bytes < 1) break; // nonstandard termination
		}

		else if (strncasecmp(buf, "NOOP", 4) != 0) {
			// Unsupported commands
			if (send_aem(sock, tls, "500 Ok\r\n", 8) != 8) {
				tlsFree(tls, &conf, &ctr_drbg, &entropy);
				return smtp_fail(sock, tls, clientIp, 108);
			}

			bytes = recv_aem(sock, tls, buf, AEM_SMTP_SIZE_CMD);
			continue;
		}

		if (send_aem(sock, tls, "250 Ok\r\n", 8) != 8) {
			tlsFree(tls, &conf, &ctr_drbg, &entropy);
			return smtp_fail(sock, tls, clientIp, 150);
		}

		bytes = recv_aem(sock, tls, buf, AEM_SMTP_SIZE_CMD);
	}

	close(sock);
	tlsFree(tls, &conf, &ctr_drbg, &entropy);
}
