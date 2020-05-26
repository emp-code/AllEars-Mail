#define AEM_ACCOUNT_RESPONSE_OK 0
#define AEM_ACCOUNT_RESPONSE_VIOLATION 10

#define AEM_API_ACCOUNT_BROWSE 10
#define AEM_API_ACCOUNT_CREATE 11
#define AEM_API_ACCOUNT_DELETE 12
#define AEM_API_ACCOUNT_UPDATE 13

#define AEM_API_ADDRESS_CREATE 20
#define AEM_API_ADDRESS_DELETE 21
#define AEM_API_ADDRESS_LOOKUP 22
#define AEM_API_ADDRESS_UPDATE 23

#define AEM_API_PRIVATE_UPDATE 40
#define AEM_API_SETTING_LIMITS 50

#define AEM_API_INTERNAL_EXIST 100
#define AEM_API_INTERNAL_LEVEL 101

#define AEM_MTA_GETPUBKEY_NORMAL 10
#define AEM_MTA_GETPUBKEY_SHIELD 11
#define AEM_MTA_ADDMESSAGE 20

#define AEM_LEN_ACCESSKEY crypto_box_SECRETKEYBYTES
#define AEM_LEN_KEY_MASTER crypto_secretbox_KEYBYTES

#define AEM_LEN_KEY_ACC crypto_box_SECRETKEYBYTES
#define AEM_LEN_KEY_API crypto_box_SECRETKEYBYTES
#define AEM_LEN_KEY_MNG crypto_secretbox_KEYBYTES
#define AEM_LEN_KEY_SIG crypto_sign_SEEDBYTES
#define AEM_LEN_KEY_STI crypto_secretbox_KEYBYTES
#define AEM_LEN_KEY_STO 32 // AES-256

#define AEM_ADDRESS_ARGON2_OPSLIMIT 3
#define AEM_ADDRESS_ARGON2_MEMLIMIT 67108864

#define AEM_ADDRESSES_PER_USER 50
#define AEM_LEN_SALT_NORM crypto_pwhash_SALTBYTES
#define AEM_LEN_SALT_SHLD crypto_shorthash_KEYBYTES
#define AEM_LEN_SALT_FAKE crypto_generichash_KEYBYTES
#define AEM_LEN_PRIVATE (4096 - crypto_box_PUBLICKEYBYTES - 1 - (AEM_ADDRESSES_PER_USER * 9))

#define AEM_MAXLEN_ADDR32 16 // 10 bytes Addr32 -> 16 characters
#define AEM_MAXLEN_DOMAIN 32

#define AEM_PORT_MTA 25
#define AEM_PORT_WEB 443
#define AEM_PORT_API 302
#define AEM_PORT_MANAGER 940

#define AEM_USERLEVEL_MAX 3
#define AEM_USERLEVEL_MIN 0

#define AEM_EXTMSG_HEADERS_LEN 30
#define AEM_EXTMSG_BODY_MAXLEN ((128 * 1024) - AEM_EXTMSG_HEADERS_LEN - crypto_sign_BYTES - crypto_box_SEALBYTES)
#define AEM_INTMSG_HEADERS_LEN 69

#define AEM_ADDR32_SYSTEM (unsigned char*)"\x36\x7d\x9d\x3a\x80\x0\x0\x0\x0\x0" // 'system' in Addr32

#define AEM_SOCKPATH_ACCOUNT "\0AEM_Acc"
#define AEM_SOCKPATH_STORAGE "\0AEM_Sto"
#define AEM_SOCKPATH_LEN 8

#define AEM_API_POST_SIZE 8192 // 8 KiB
#define AEM_API_POST_BOXED_SIZE (crypto_box_NONCEBYTES + crypto_box_PUBLICKEYBYTES + AEM_API_POST_SIZE + 2 + crypto_box_MACBYTES)
