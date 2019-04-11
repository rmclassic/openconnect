/*
 * OpenConnect (SSL + DTLS) VPN client
 *
 * Copyright © 2008-2015 Intel Corporation.
 *
 * Author: David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <config.h>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "openconnect-internal.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)

#define EVP_CIPHER_CTX_free(c) do {				\
				    EVP_CIPHER_CTX_cleanup(c);	\
				    free(c); } while (0)
#define HMAC_CTX_free(c) do {					\
				    HMAC_CTX_cleanup(c);	\
				    free(c); } while (0)
#define HMAC_CTX_reset HMAC_CTX_cleanup

static inline HMAC_CTX *HMAC_CTX_new(void)
{
	HMAC_CTX *ret = malloc(sizeof(*ret));
	if (ret)
		HMAC_CTX_init(ret);
	return ret;
}
#endif

void destroy_esp_ciphers(struct esp *esp)
{
	if (esp->cipher) {
		EVP_CIPHER_CTX_free(esp->cipher);
		esp->cipher = NULL;
	}
	if (esp->hmac) {
		HMAC_CTX_free(esp->hmac);
		esp->hmac = NULL;
	}
	if (esp->pkt_hmac) {
		HMAC_CTX_free(esp->pkt_hmac);
		esp->pkt_hmac = NULL;
	}
}

static int init_esp_ciphers(struct openconnect_info *vpninfo, struct esp *esp,
			    const EVP_MD *macalg, const EVP_CIPHER *encalg, int decrypt)
{
	int ret;

	destroy_esp_ciphers(esp);

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	esp->cipher = malloc(sizeof(*esp->cipher));
	if (!esp->cipher)
		return -ENOMEM;
	EVP_CIPHER_CTX_init(esp->cipher);
#else
	esp->cipher = EVP_CIPHER_CTX_new();
	if (!esp->cipher)
		return -ENOMEM;
#endif

	if (decrypt)
		ret = EVP_DecryptInit_ex(esp->cipher, encalg, NULL, esp->enc_key, NULL);
	else {
		ret = RAND_bytes((void *)esp->iv, sizeof(esp->iv)) &&
			EVP_EncryptInit_ex(esp->cipher, encalg, NULL, esp->enc_key, esp->iv);
	}
	if (!ret) {
		vpn_progress(vpninfo, PRG_ERR,
			     _("Failed to initialise ESP cipher:\n"));
		openconnect_report_ssl_errors(vpninfo);
		return -EIO;
	}
	EVP_CIPHER_CTX_set_padding(esp->cipher, 0);

	esp->hmac = HMAC_CTX_new();
	esp->pkt_hmac = HMAC_CTX_new();
	if (!esp->hmac || !esp->pkt_hmac) {
		destroy_esp_ciphers(esp);
		return -ENOMEM;
	}
	if (!HMAC_Init_ex(esp->hmac, esp->hmac_key,
			  EVP_MD_size(macalg), macalg, NULL)) {
		vpn_progress(vpninfo, PRG_ERR,
			     _("Failed to initialize ESP HMAC\n"));

		openconnect_report_ssl_errors(vpninfo);
		destroy_esp_ciphers(esp);
		return -EIO;
	}

	esp->seq = 0;
	esp->seq_backlog = 0;
	return 0;
}

int setup_esp_keys(struct openconnect_info *vpninfo, int new_keys)
{
	struct esp *esp_in;
	const EVP_CIPHER *encalg;
	const EVP_MD *macalg;
	int ret;

	if (vpninfo->dtls_state == DTLS_DISABLED)
		return -EOPNOTSUPP;
	if (!vpninfo->dtls_addr)
		return -EINVAL;

	switch (vpninfo->esp_enc) {
	case 0x02:
		encalg = EVP_aes_128_cbc();
		break;
	case 0x05:
		encalg = EVP_aes_256_cbc();
		break;
	default:
		return -EINVAL;
	}

	switch (vpninfo->esp_hmac) {
	case 0x01:
		macalg = EVP_md5();
		break;
	case 0x02:
		macalg = EVP_sha1();
		break;
	default:
		return -EINVAL;
	}

	if (new_keys) {
		vpninfo->old_esp_maxseq = vpninfo->esp_in[vpninfo->current_esp_in].seq + 32;
		vpninfo->current_esp_in ^= 1;
	}

	esp_in = &vpninfo->esp_in[vpninfo->current_esp_in];

	if (new_keys) {
		if (!RAND_bytes((void *)&esp_in->spi, sizeof(esp_in->spi)) ||
		    !RAND_bytes((void *)&esp_in->enc_key, vpninfo->enc_key_len) ||
		    !RAND_bytes((void *)&esp_in->hmac_key, vpninfo->hmac_key_len) ) {
			vpn_progress(vpninfo, PRG_ERR,
				     _("Failed to generate random keys for ESP:\n"));
			openconnect_report_ssl_errors(vpninfo);
			return -EIO;
		}
	}

	ret = init_esp_ciphers(vpninfo, &vpninfo->esp_out, macalg, encalg, 0);
	if (ret)
		return ret;

	ret = init_esp_ciphers(vpninfo, esp_in, macalg, encalg, 1);
	if (ret) {
		destroy_esp_ciphers(&vpninfo->esp_out);
		return ret;
	}

	if (vpninfo->dtls_state == DTLS_NOSECRET)
		vpninfo->dtls_state = DTLS_SECRET;
	vpninfo->pkt_trailer = 16 + 20; /* 16 for pad, 20 for HMAC (of which we use 16) */
	return 0;
}

/* pkt->len shall be the *payload* length. Omitting the header and the 12-byte HMAC */
int decrypt_esp_packet(struct openconnect_info *vpninfo, struct esp *esp, struct pkt *pkt)
{
	unsigned char hmac_buf[20];
	unsigned int hmac_len = sizeof(hmac_buf);
	int crypt_len = pkt->len;

	HMAC_CTX_copy(esp->pkt_hmac, esp->hmac);
	HMAC_Update(esp->pkt_hmac, (void *)&pkt->esp, sizeof(pkt->esp) + pkt->len);
	HMAC_Final(esp->pkt_hmac, hmac_buf, &hmac_len);
	HMAC_CTX_reset(esp->pkt_hmac);

	if (memcmp(hmac_buf, pkt->data + pkt->len, 12)) {
		vpn_progress(vpninfo, PRG_DEBUG,
			     _("Received ESP packet with invalid HMAC\n"));
		return -EINVAL;
	}

	if (verify_packet_seqno(vpninfo, esp, ntohl(pkt->esp.seq)))
		return -EINVAL;

	if (!EVP_DecryptInit_ex(esp->cipher, NULL, NULL, NULL,
				pkt->esp.iv)) {
		vpn_progress(vpninfo, PRG_ERR,
			     _("Failed to set up decryption context for ESP packet:\n"));
		openconnect_report_ssl_errors(vpninfo);
		return -EINVAL;
	}

	if (!EVP_DecryptUpdate(esp->cipher, pkt->data, &crypt_len,
			       pkt->data, pkt->len)) {
		vpn_progress(vpninfo, PRG_ERR,
			     _("Failed to decrypt ESP packet:\n"));
		openconnect_report_ssl_errors(vpninfo);
		return -EINVAL;
	}

	return 0;
}

static int old_encrypt_esp_packet(struct openconnect_info *vpninfo, struct pkt *pkt)
{
	int i, padlen;
	const int blksize = 16;
	unsigned int hmac_len = 20;
	int crypt_len;

	/* This gets much more fun if the IV is variable-length */
	pkt->esp.spi = vpninfo->esp_out.spi;
	pkt->esp.seq = htonl(vpninfo->esp_out.seq++);

	/* This is the current IV from the EVP_CIPHER_CTX */
	memcpy(pkt->esp.iv, vpninfo->esp_out.iv, sizeof(pkt->esp.iv));

	padlen = blksize - 1 - ((pkt->len + 1) % blksize);
	for (i=0; i<padlen; i++)
		pkt->data[pkt->len + i] = i + 1;
	pkt->data[pkt->len + padlen] = padlen;
	pkt->data[pkt->len + padlen + 1] = 0x04; /* Legacy IP */

	crypt_len = pkt->len + padlen + 2;
	if (!EVP_EncryptUpdate(vpninfo->esp_out.cipher, pkt->data, &crypt_len,
			       pkt->data, crypt_len)) {
		vpn_progress(vpninfo, PRG_ERR,
			     _("Failed to encrypt ESP packet:\n"));
		openconnect_report_ssl_errors(vpninfo);
		return -EINVAL;
	}

	/* IV for next time. */
	memcpy(vpninfo->esp_out.iv, pkt->data + crypt_len - 16, 16);

	HMAC_Init_ex(vpninfo->esp_out.hmac, NULL, 0, NULL, NULL);
	HMAC_Update(vpninfo->esp_out.hmac, (void *)&pkt->esp, sizeof(pkt->esp) + crypt_len);
	HMAC_Final(vpninfo->esp_out.hmac, pkt->data + crypt_len, &hmac_len);

	return sizeof(pkt->esp) + crypt_len + 12;
}
int OPENSSL_ia32cap_P[4] = { -1, -1, -1, -1 };
#if 1
#include <openssl/aes.h>

#define HMAC_MAX_MD_CBLOCK_SIZE     144

struct hmac_ctx_st {
    const EVP_MD *md;
    EVP_MD_CTX *md_ctx;
    EVP_MD_CTX *i_ctx;
    EVP_MD_CTX *o_ctx;
    unsigned int key_length;
    unsigned char key[HMAC_MAX_MD_CBLOCK_SIZE];
};
void aesni_cbc_encrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length,
                       const AES_KEY *key, unsigned char *ivec, int enc);
void aesni_cbc_sha1_enc(const void *inp, void *out, size_t blocks,
                        const AES_KEY *key, unsigned char iv[16],
                        SHA_CTX *ctx, const void *in0);


int encrypt_esp_packet(struct openconnect_info *vpninfo, struct pkt *pkt)
{
	int i, padlen;
	const int blksize = 16;
	unsigned int hmac_len = 20;
	int crypt_len;
	AES_KEY *ak;
	int stitched;
	SHA_CTX *sha;

#define PRECBC 64
#define BLK 64
	if (pkt->len < PRECBC + BLK)
		return old_encrypt_esp_packet(vpninfo, pkt);

	/* This gets much more fun if the IV is variable-length */
	pkt->esp.spi = vpninfo->esp_out.spi;
	pkt->esp.seq = htonl(vpninfo->esp_out.seq++);

	memcpy(pkt->esp.iv, vpninfo->esp_out.iv, sizeof(pkt->esp.iv));

	padlen = blksize - 1 - ((pkt->len + 1) % blksize);
	for (i=0; i<padlen; i++)
		pkt->data[pkt->len + i] = i + 1;
	pkt->data[pkt->len + padlen] = padlen;
	pkt->data[pkt->len + padlen + 1] = 0x04; /* Legacy IP */

	crypt_len = pkt->len + padlen + 2;

	/* It's actually an EVP_AES_KEY but the AES_KEY is first in that. */
	ak = EVP_CIPHER_CTX_get_cipher_data(vpninfo->esp_out.cipher);
	/* Encrypt the first block */
	aesni_cbc_encrypt(pkt->data, pkt->data, PRECBC, ak, (unsigned char *)&vpninfo->esp_out.iv, 1);

	/* Then the stitched part */
	stitched = (crypt_len-PRECBC) / BLK;
	HMAC_Init_ex(vpninfo->esp_out.hmac, NULL, 0, NULL, NULL);
	sha = EVP_MD_CTX_md_data(vpninfo->esp_out.hmac->md_ctx);
	aesni_cbc_sha1_enc(pkt->data + PRECBC, pkt->data + PRECBC, stitched, ak, (unsigned char *)&vpninfo->esp_out.iv,
			   sha, &pkt->esp);
	sha->Nl += (stitched * BLK) << 3;

	//	printf("hashed %d bytes at %p; next %p\n", stitched * BLK, &pkt->esp, ((void *)&pkt->esp)+(stitched * BLK));

#if 0 /* Hm, the hashing isn't working right ... */
	HMAC_CTX_copy(vpninfo->esp_out.pkt_hmac, vpninfo->esp_out.hmac);
	sha = EVP_MD_CTX_md_data(vpninfo->esp_out.pkt_hmac->md_ctx);
	SHA1_Update(sha, (void *)&pkt->esp, stitched * BLK);
	printf("rehash %d bytes at %p, next %p\n", stitched * BLK, &pkt->esp,
	       ((void *)&pkt->esp) + (stitched * BLK));
#endif

	/* Now encrypt anything remaining */
	stitched *= BLK;
	stitched += PRECBC; /* We pre-encrypted one block for EtM */
	if (crypt_len > stitched)
		aesni_cbc_encrypt(pkt->data + stitched, pkt->data + stitched,
				  crypt_len - stitched, ak, (unsigned char *)&vpninfo->esp_out.iv, 1);

	/* And now fold in the final part of the HMAC, which is two blocks plus the ESP header behind */
	stitched -= PRECBC + sizeof(pkt->esp);
	HMAC_Update(vpninfo->esp_out.hmac, (void *)&pkt->data[stitched], crypt_len - stitched);
	//	printf("hashed %d more bytes at %p\n", crypt_len - stitched, &pkt->data[stitched]);
	//	printf("crypt_len %d, stitched %d, esp %p, data %p\n", crypt_len, stitched, &pkt->esp, &pkt->data);
	HMAC_Final(vpninfo->esp_out.hmac, pkt->data + crypt_len, &hmac_len);

	return sizeof(pkt->esp) + crypt_len + 12;
}
#endif
