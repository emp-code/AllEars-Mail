#ifndef AEM_SMTP_H
#define AEM_SMTP_H

void respond_smtp(int sock, mbedtls_x509_crt *srvcert, mbedtls_pk_context *pkey, const uint32_t clientIp, const unsigned char seed[16], const size_t lenDomain, const char *domain);

#endif
