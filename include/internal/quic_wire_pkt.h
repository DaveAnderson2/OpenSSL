/*
 * Copyright 2022 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OSSL_QUIC_WIRE_PKT_H
# define OSSL_QUIC_WIRE_PKT_H

# include <openssl/ssl.h>
# include "internal/packet.h"
# include "internal/quic_types.h"

# define QUIC_VERSION_NONE   ((uint32_t)0)   /* Used for version negotiation */
# define QUIC_VERSION_1      ((uint32_t)1)   /* QUIC v1 */

/* QUIC logical packet type. These do not match wire values. */
# define QUIC_PKT_TYPE_INITIAL        1
# define QUIC_PKT_TYPE_0RTT           2
# define QUIC_PKT_TYPE_HANDSHAKE      3
# define QUIC_PKT_TYPE_RETRY          4
# define QUIC_PKT_TYPE_1RTT           5
# define QUIC_PKT_TYPE_VERSION_NEG    6

/*
 * Smallest possible QUIC packet size as per RFC (aside from version negotiation
 * packets).
 */
#define QUIC_MIN_VALID_PKT_LEN_CRYPTO      21
#define QUIC_MIN_VALID_PKT_LEN_VERSION_NEG  7
#define QUIC_MIN_VALID_PKT_LEN              QUIC_MIN_VALID_PKT_LEN_VERSION_NEG

typedef struct quic_pkt_hdr_ptrs_st QUIC_PKT_HDR_PTRS;

/*
 * QUIC Packet Header Protection
 * =============================
 *
 * Functions to apply and remove QUIC packet header protection. A header
 * protector is initialised using ossl_quic_hdr_protector_init and must be
 * destroyed using ossl_quic_hdr_protector_destroy when no longer needed.
 */
typedef struct quic_hdr_protector_st {
    OSSL_LIB_CTX       *libctx;
    const char         *propq;
    EVP_CIPHER_CTX     *cipher_ctx;
    EVP_CIPHER         *cipher;
    uint32_t            cipher_id;
} QUIC_HDR_PROTECTOR;

# define QUIC_HDR_PROT_CIPHER_AES_128    1
# define QUIC_HDR_PROT_CIPHER_AES_256    2
# define QUIC_HDR_PROT_CIPHER_CHACHA     3

/*
 * Initialises a header protector.
 *
 *   cipher_id:
 *      The header protection cipher method to use. One of
 *      QUIC_HDR_PROT_CIPHER_*. Must be chosen based on negotiated TLS cipher
 *      suite.
 *
 *   quic_hp_key:
 *      This must be the "quic hp" key derived from a traffic secret.
 *
 *      The length of the quic_hp_key must correspond to that expected for the
 *      given cipher ID.
 *
 * The header protector performs amortisable initialisation in this function,
 * therefore a header protector should be used for as long as possible.
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_hdr_protector_init(QUIC_HDR_PROTECTOR *hpr,
                                 OSSL_LIB_CTX *libctx,
                                 const char *propq,
                                 uint32_t cipher_id,
                                 const unsigned char *quic_hp_key,
                                 size_t quic_hp_key_len);

/*
 * Destroys a header protector. This is also safe to call on a zero-initialized
 * OSSL_QUIC_HDR_PROTECTOR structure which has not been initialized, or which
 * has already been destroyed.
 */
void ossl_quic_hdr_protector_destroy(QUIC_HDR_PROTECTOR *hpr);

/*
 * Removes header protection from a packet. The packet payload must currently be
 * encrypted (i.e., you must remove header protection before decrypting packets
 * received). The function examines the header buffer to determine which bytes
 * of the header need to be decrypted.
 *
 * If this function fails, no data is modified.
 *
 * This is implemented as a call to ossl_quic_hdr_protector_decrypt_fields().
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_hdr_protector_decrypt(QUIC_HDR_PROTECTOR *hpr,
                                    QUIC_PKT_HDR_PTRS *ptrs);

/*
 * Applies header protection to a packet. The packet payload must already have
 * been encrypted (i.e., you must apply header protection after encrypting
 * a packet). The function examines the header buffer to determine which bytes
 * of the header need to be encrypted.
 *
 * This is implemented as a call to ossl_quic_hdr_protector_encrypt_fields().
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_hdr_protector_encrypt(QUIC_HDR_PROTECTOR *hpr,
                                    QUIC_PKT_HDR_PTRS *ptrs);

/*
 * Removes header protection from a packet. The packet payload must currently
 * be encrypted. This is a low-level function which assumes you have already
 * determined which parts of the packet header need to be decrypted.
 *
 * sample:
 *   The range of bytes in the packet to be used to generate the header
 *   protection mask. It is permissible to set sample_len to the size of the
 *   remainder of the packet; this function will only use as many bytes as
 *   needed. If not enough sample bytes are provided, this function fails.
 *
 * first_byte:
 *   The first byte of the QUIC packet header to be decrypted.
 *
 * pn:
 *   Pointer to the start of the PN field. The caller is responsible
 *   for ensuring at least four bytes follow this pointer.
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_hdr_protector_decrypt_fields(QUIC_HDR_PROTECTOR *hpr,
                                           const unsigned char *sample,
                                           size_t sample_len,
                                           unsigned char *first_byte,
                                           unsigned char *pn_bytes);

/*
 * Works analogously to ossl_hdr_protector_decrypt_fields, but applies header
 * protection instead of removing it.
 */
int ossl_quic_hdr_protector_encrypt_fields(QUIC_HDR_PROTECTOR *hpr,
                                           const unsigned char *sample,
                                           size_t sample_len,
                                           unsigned char *first_byte,
                                           unsigned char *pn_bytes);

/*
 * QUIC Packet Header
 * ==================
 *
 * This structure provides a logical representation of a QUIC packet header.
 *
 * QUIC packet formats fall into the following categories:
 *
 *   Long Packets, which is subdivided into five possible packet types:
 *     Version Negotiation (a special case);
 *     Initial;
 *     0-RTT;
 *     Handshake; and
 *     Retry
 *
 *   Short Packets, which comprises only a single packet type (1-RTT).
 *
 * The packet formats vary and common fields are found in some packets but
 * not others. The below table indicates which fields are present in which
 * kinds of packet. * indicates header protection is applied.
 *
 *   SLLLLL         Legend: 1=1-RTT, i=Initial, 0=0-RTT, h=Handshake
 *   1i0hrv                 r=Retry, v=Version Negotiation
 *   ------
 *   1i0hrv         Header Form (0=Short, 1=Long)
 *   1i0hr          Fixed Bit (always 1)
 *   1              Spin Bit
 *   1       *      Reserved Bits
 *   1       *      Key Phase
 *   1i0h    *      Packet Number Length
 *    i0hr?         Long Packet Type
 *    i0h           Type-Specific Bits
 *    i0hr          Version (note: always 0 for Version Negotiation packets)
 *   1i0hrv         Destination Connection ID
 *    i0hrv         Source Connection ID
 *   1i0h    *      Packet Number
 *    i             Token
 *    i0h           Length
 *       r          Retry Token
 *       r          Retry Integrity Tag
 *
 * For each field below, the conditions under which the field is valid are
 * specified. If a field is not currently valid, it is initialized to a zero or
 * NULL value.
 */
typedef struct quic_pkt_hdr_st {
    /* [ALL] A QUIC_PKT_TYPE_* value. Always valid. */
    unsigned int    type        :8;

    /* [S] Value of the spin bit. Valid if (type == 1RTT). */
    unsigned int    spin_bit    :1;

    /*
     * [S] Value of the Key Phase bit in the short packet.
     * Valid if (type == 1RTT && !partial).
     */
    unsigned int    key_phase   :1;

    /*
     * [1i0h] Length of packet number in bytes. This is the decoded value.
     * Valid if ((type == 1RTT || (version && type != RETRY)) && !partial).
     */
    unsigned int    pn_len      :4;

    /*
     * [ALL] Set to 1 if this is a partial decode because the packet header
     * has not yet been deprotected. pn_len, pn and key_phase are not valid if
     * this is set.
     */
    unsigned int    partial     :1;

    /*
     * [ALL] Whether the fixed bit was set. Note that only Version Negotiation
     * packets are allowed to have this unset, so this will always be 1 for all
     * other packet types (decode will fail if it is not set). Ignored when
     * encoding unless encoding a Version Negotiation packet.
     */
    unsigned int    fixed       :1;

    /* [L] Version field. Valid if (type != 1RTT). */
    uint32_t        version;

    /* [ALL] Number of bytes in the connection ID (max 20). Always valid. */
    QUIC_CONN_ID    dst_conn_id;

    /*
     * [L] Number of bytes in the connection ID (max 20).
     * Valid if (type != 1RTT).
     */
    QUIC_CONN_ID    src_conn_id;

    /*
     * [1i0h] Relatively-encoded packet number in raw, encoded form. The correct
     * decoding of this value is context-dependent. The number of bytes valid in
     * this buffer is determined by pn_len above. If the decode was partial,
     * this field is not valid.
     *
     * Valid if ((type == 1RTT || (version && type != RETRY)) && !partial).
     */
    unsigned char           pn[4];

    /*
     * [i] Token field in Initial packet. Points to memory inside the decoded
     * PACKET, and therefore is valid for as long as the PACKET's buffer is
     * valid. token_len is the length of the token in bytes.
     *
     * Valid if (type == INITIAL).
     */
    const unsigned char    *token;
    size_t                  token_len;

    /*
     * [i0h] Payload length in bytes.
     *
     * Valid if (type != 1RTT && type != RETRY && version).
     */
    size_t                  len;

    /*
     * Pointer to start of payload data in the packet. Points to memory inside
     * the decoded PACKET, and therefore is valid for as long as the PACKET'S
     * buffer is valid. The length of the buffer in bytes is in len above.
     *
     * For Version Negotiation packets, points to the array of supported
     * versions.
     *
     * For Retry packets, points to the Retry packet payload, which comprises
     * the Retry Token followed by a 16-byte Retry Integrity Tag.
     *
     * Regardless of whether a packet is a Version Negotiation packet (where the
     * payload contains a list of supported versions), a Retry packet (where the
     * payload contains a Retry Token and Retry Integrity Tag), or any other
     * packet type (where the payload contains frames), the payload is not
     * validated and the user must parse the payload bearing this in mind.
     *
     * If the decode was partial (partial is set), this points to the start of
     * the packet number field, rather than the protected payload, as the length
     * of the packet number field is unknown. The len field reflects this in
     * this case (i.e., the len field is the number of payload bytes plus the
     * number of bytes comprising the PN).
     */
    const unsigned char    *data;
} QUIC_PKT_HDR;

/*
 * Extra information which can be output by the packet header decode functions
 * for the assistance of the header protector. This avoids the header protector
 * needing to partially re-decode the packet header.
 */
struct quic_pkt_hdr_ptrs_st {
    unsigned char    *raw_start;        /* start of packet */
    unsigned char    *raw_sample;       /* start of sampling range */
    size_t            raw_sample_len;   /* maximum length of sampling range */

    /*
     * Start of PN field. Guaranteed to be NULL unless at least four bytes are
     * available via this pointer.
     */
    unsigned char    *raw_pn;
};

/*
 * If partial is 1, reads the unprotected parts of a protected packet header
 * from a PACKET, performing a partial decode.
 *
 * If partial is 0, the input is assumed to have already had header protection
 * removed, and all header fields are decoded.
 *
 * On success, the logical decode of the packet header is written to *hdr.
 * hdr->partial is set or cleared according to whether a partial decode was
 * performed. *ptrs is filled with pointers to various parts of the packet
 * buffer.
 *
 * In order to decode short packets, the connection ID length being used must be
 * known contextually, and should be passed as short_conn_id_len. If
 * short_conn_id_len is set to an invalid value (a value greater than
 * QUIC_MAX_CONN_ID_LEN), this function fails when trying to decode a short
 * packet, but succeeds for long packets.
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_wire_decode_pkt_hdr(PACKET *pkt,
                                  size_t short_conn_id_len,
                                  int partial,
                                  QUIC_PKT_HDR *hdr,
                                  QUIC_PKT_HDR_PTRS *ptrs);

/*
 * Encodes a packet header. The packet is written to pkt.
 *
 * The length of the (encrypted) packet payload should be written to hdr->len
 * and will be placed in the serialized packet header. The payload data itself
 * is not copied; the caller should write hdr->len bytes of encrypted payload to
 * the WPACKET immediately after the call to this function. However,
 * WPACKET_reserve_bytes is called for the payload size.
 *
 * This function does not apply header protection. You must apply header
 * protection yourself after calling this function. *ptrs is filled with
 * pointers which can be passed to a header protector, but this must be
 * performed after the encrypted payload is written.
 *
 * The pointers in *ptrs are direct pointers into the WPACKET buffer. If more
 * data is written to the WPACKET buffer, WPACKET buffer reallocations may
 * occur, causing these pointers to become invalid. Therefore, you must not call
 * any write WPACKET function between this call and the call to
 * ossl_quic_hdr_protector_encrypt. This function calls WPACKET_reserve_bytes
 * for the payload length, so you may assume hdr->len bytes are already free to
 * write at the WPACKET cursor location once this function returns successfully.
 * It is recommended that you call this function, write the encrypted payload,
 * call ossl_quic_hdr_protector_encrypt, and then call
 * WPACKET_allocate_bytes(hdr->len).
 *
 * Version Negotiation and Retry packets do not use header protection; for these
 * header types, the fields in *ptrs are all written as zero. Version
 * Negotiation, Retry and 1-RTT packets do not contain a Length field, but
 * hdr->len bytes of data are still reserved in the WPACKET.
 *
 * If serializing a short packet and short_conn_id_len does not match the DCID
 * specified in hdr, the function fails.
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_wire_encode_pkt_hdr(WPACKET *pkt,
                                  size_t short_conn_id_len,
                                  const QUIC_PKT_HDR *hdr,
                                  QUIC_PKT_HDR_PTRS *ptrs);

/*
 * Retrieves only the DCID from a packet header. This is intended for demuxer
 * use. It avoids the need to parse the rest of the packet header twice.
 *
 * Information on packet length is not decoded, as this only needs to be used on
 * the first packet in a datagram, therefore this takes a buffer and not a
 * PACKET.
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_wire_get_pkt_hdr_dst_conn_id(const unsigned char *buf,
                                           size_t buf_len,
                                           size_t short_conn_id_len,
                                           QUIC_CONN_ID *dst_conn_id);

/*
 * Packet Number Encoding
 * ======================
 */

/*
 * Decode an encoded packet header QUIC PN.
 *
 * enc_pn is the raw encoded PN to decode. enc_pn_len is its length in bytes as
 * indicated by packet headers. largest_pn is the largest PN successfully
 * processed in the relevant PN space.
 *
 * The resulting PN is written to *res_pn.
 *
 * Returns 1 on success or 0 on failure.
 */
int ossl_quic_wire_decode_pkt_hdr_pn(const unsigned char *enc_pn,
                                     size_t enc_pn_len,
                                     QUIC_PN largest_pn,
                                     QUIC_PN *res_pn);

/*
 * Determine how many bytes should be used to encode a PN. Returns the number of
 * bytes (which will be in range [1, 4]).
 */
int ossl_quic_wire_determine_pn_len(QUIC_PN pn, QUIC_PN largest_acked);

/*
 * Encode a PN for a packet header using the specified number of bytes, which
 * should have been determined by calling ossl_quic_wire_determine_pn_len. The
 * PN encoding process is done in two parts to allow the caller to override PN
 * encoding length if it wishes.
 *
 * Returns 1 on success and 0 on failure.
 */
int ossl_quic_wire_encode_pkt_hdr_pn(QUIC_PN pn,
                                     unsigned char *enc_pn,
                                     size_t enc_pn_len);
#endif