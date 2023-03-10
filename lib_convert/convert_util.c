/**
 * Copyright(c) 2019, Tessares S.A.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and / or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *        SERVICES; LOSS OF USE, DATA, OR PROFITS;
 *        OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <stdlib.h>

#include "convert_util.h"

#define CONVERT_TO_BYTES(v) (v * CONVERT_PADDING)
#define BYTES_TO_CONVERT(v) (v / CONVERT_PADDING)

int convert_parse_header(const uint8_t *buff, size_t buff_len, size_t *tlvs_length)
{
	struct convert_header *hdr = (struct convert_header *)buff;

	if (buff_len != sizeof(*hdr))
	{
		printf("buff_len != sizeof(*hdr)\n");
		return -1;
	}

	/* only support a single version */
	if (hdr->version != CONVERT_VERSION)
	{
		printf("hdr->version = %d != CONVERT_VERSION\n", hdr->version);
		return -1;
	}

	if (ntohs(hdr->magic_no) != CONVERT_MAGIC_NO)
	{
		printf("ntohs(hdr->magic_no) = %d != CONVERT_MAGIC_NO\n", ntohs(hdr->magic_no));
		return -1;
	}

	*tlvs_length = CONVERT_TO_BYTES(hdr->total_length) - sizeof(*hdr);
	return 0;
}

void convert_free_opts(struct convert_opts *opts)
{
	free(opts->tcp_options);
	free(opts->cookie_data);
	free(opts);
}

struct convert_opts *
convert_parse_tlvs(const uint8_t *buff, size_t buff_len)
{
	struct convert_opts *opts;

	if (buff_len == 0)
		return NULL;

	opts = calloc(sizeof(struct convert_opts), 1);
	if (!opts)
		return NULL;

	while (buff_len > 0)
	{
		struct convert_tlv *tlv = (struct convert_tlv *)buff;
		size_t tlv_len;

		if (buff_len < CONVERT_ALIGN(sizeof(*tlv)))
			goto error_and_free;

		tlv_len = CONVERT_TO_BYTES(tlv->length);

		if (buff_len < tlv_len)
			goto error_and_free;

		switch (tlv->type)
		{
		case CONVERT_ERROR:
		{
			struct convert_error *error =
				(struct convert_error *)buff;

			if (buff_len < CONVERT_ALIGN(sizeof(*error)))
				goto error_and_free;

			opts->flags |= CONVERT_F_ERROR;
			opts->error_code = error->error_code;
			/* TODO support handling the field: value */

			break;
		}
		case CONVERT_CONNECT:
		{
			struct convert_connect *conv_connect =
				(struct convert_connect *)buff;

			if (buff_len < CONVERT_ALIGN(sizeof(*conv_connect)))
				goto error_and_free;

			/* TODO support the options. */
			if (CONVERT_TO_BYTES(tlv->length) !=
				CONVERT_ALIGN(sizeof(*conv_connect)))
				goto error_and_free;

			opts->flags |= CONVERT_F_CONNECT;
			/* conv_connect comes from the network, and thus is in
			 * network byte order. The sin6_port and sin6_addr
			 * members of remote_addr shall also be in network byte
			 * order.
			 */
			opts->remote_addr.sin6_addr =
				conv_connect->remote_addr;
			opts->remote_addr.sin6_port =
				conv_connect->remote_port;

			break;
		}
		case CONVERT_EXTENDED_TCP_HDR:
		{
			struct convert_extended_tcp_hdr *conv_ext_tcp_hdr =
				(struct convert_extended_tcp_hdr *)buff;
			size_t tcp_options_len =
				tlv_len -
				sizeof(struct convert_extended_tcp_hdr);

			if (buff_len < CONVERT_ALIGN(sizeof(*conv_ext_tcp_hdr)))
				goto error_and_free;

			opts->flags |= CONVERT_F_EXTENDED_TCP_HDR;

			opts->tcp_options_len = tcp_options_len;
			opts->tcp_options = malloc(tcp_options_len);
			if (opts->tcp_options == NULL)
				goto error_and_free;
			memcpy(opts->tcp_options, conv_ext_tcp_hdr->tcp_options,
				   tcp_options_len);

			break;
		}
		case CONVERT_COOKIE:
		{
			struct convert_cookie *cookie =
				(struct convert_cookie *)buff;
			size_t cookie_len =
				tlv_len - sizeof(struct convert_cookie);

			opts->flags |= CONVERT_F_COOKIE;

			opts->cookie_len = cookie_len;
			opts->cookie_data = malloc(cookie_len);
			if (opts->cookie_data == NULL)
				goto error_and_free;
			memcpy(opts->cookie_data, cookie->opaque, cookie_len);

			break;
		}
		/* TODO support other TLVs. */
		default:
			goto error_and_free;
		}

		buff += tlv_len;
		buff_len -= tlv_len;
	}

	return opts;

error_and_free:
	free(opts);
	return NULL;
}

static ssize_t
_convert_write_tlv_not_supp(UNUSED uint8_t *buff, size_t UNUSED buff_len,
							UNUSED const struct convert_opts *opts)
{
	return -1;
}

static ssize_t
_convert_write_tlv_connect(uint8_t *buff, size_t buff_len,
						   const struct convert_opts *opts)
{
	struct convert_connect *conv_connect = (struct convert_connect *)buff;
	size_t length =
		CONVERT_ALIGN(sizeof(*conv_connect));

	if (buff_len < length)
		return -1;

	conv_connect->remote_addr = opts->remote_addr.sin6_addr;
	conv_connect->remote_port = opts->remote_addr.sin6_port;

	return length;
}

static ssize_t
_convert_write_tlv_error(uint8_t *buff, size_t buff_len,
						 const struct convert_opts *opts)
{
	struct convert_error *error = (struct convert_error *)buff;
	size_t length = CONVERT_ALIGN(sizeof(*error));

	if (buff_len < length)
		return -1;

	error->error_code = opts->error_code;

	return length;
}

static ssize_t
_convert_write_tlv_extended_tcp_hdr(uint8_t *buff, size_t buff_len,
									const struct convert_opts *opts)
{
	struct convert_extended_tcp_hdr *ext_tcp_hdr =
		(struct convert_extended_tcp_hdr *)buff;
	size_t length = CONVERT_ALIGN(
		sizeof(*ext_tcp_hdr) + opts->tcp_options_len);

	if (buff_len < length)
		return -1;

	memset(ext_tcp_hdr, '\0', length);
	memcpy(ext_tcp_hdr->tcp_options, opts->tcp_options,
		   opts->tcp_options_len);

	return length;
}

static ssize_t
_convert_write_tlv_cookie(uint8_t *buff, size_t buff_len,
						  const struct convert_opts *opts)
{
	struct convert_cookie *cookie = (struct convert_cookie *)buff;
	size_t length = CONVERT_ALIGN(sizeof(*cookie) +
								  opts->cookie_len);

	if (buff_len < length)
		return -1;

	memset(cookie, '\0', length);
	memcpy(cookie->opaque, opts->cookie_data, opts->cookie_len);

	return length;
}

static struct
{
	uint32_t flag;
	uint8_t type;
	ssize_t (*cb)(uint8_t *buff, size_t buff_len,
				  const struct convert_opts *opts);
} _converter_tlvs[_CONVERT_F_MAX] = {
	[_CONVERT_F_INFO] = {
		.flag = CONVERT_F_INFO,
		.type = CONVERT_INFO,
		.cb = _convert_write_tlv_not_supp,
	},
	[_CONVERT_F_CONNECT] = {
		.flag = CONVERT_F_CONNECT,
		.type = CONVERT_CONNECT,
		.cb = _convert_write_tlv_connect,
	},
	[_CONVERT_F_EXTENDED_TCP_HDR] = {
		.flag = CONVERT_F_EXTENDED_TCP_HDR,
		.type = CONVERT_EXTENDED_TCP_HDR,
		.cb = _convert_write_tlv_extended_tcp_hdr,
	},
	[_CONVERT_F_SUPPORTED_TCP_EXT] = {
		.flag = CONVERT_F_SUPPORTED_TCP_EXT,
		.type = CONVERT_SUPPORTED_TCP_EXT,
		.cb = _convert_write_tlv_not_supp,
	},
	[_CONVERT_F_COOKIE] = {
		.flag = CONVERT_F_COOKIE,
		.type = CONVERT_COOKIE,
		.cb = _convert_write_tlv_cookie,
	},
	[_CONVERT_F_ERROR] = {
		.flag = CONVERT_F_ERROR,
		.type = CONVERT_ERROR,
		.cb = _convert_write_tlv_error,
	},
};

ssize_t
_convert_write_tlvs(uint8_t *buff, size_t buff_len,
					const struct convert_opts *opts)
{
	uint8_t flags = opts->flags;
	ssize_t len = 0;
	int i;

	for (i = 0; i < _CONVERT_F_MAX; ++i)
	{
		struct convert_tlv *tlv = (struct convert_tlv *)buff;
		ssize_t ret;

		if (!(_converter_tlvs[i].flag & flags))
			continue;

		ret = _converter_tlvs[i].cb(buff, buff_len, opts);
		if (ret < 0)
			return ret;

		tlv->type = _converter_tlvs[i].type;
		tlv->length = BYTES_TO_CONVERT(ret);

		len += ret;
		buff += ret;
		buff_len -= ret;

		flags &= ~(_converter_tlvs[i].flag);
	}

	return len;
}

ssize_t
convert_write(uint8_t *buff, size_t buff_len, const struct convert_opts *opts)
{
	struct convert_header *hdr = (struct convert_header *)buff;
	size_t length = sizeof(*hdr);
	ssize_t ret;

	memset(buff, 0, buff_len);

	if (buff_len < length)
		return -1;

	hdr->version = CONVERT_VERSION;
	hdr->magic_no = htons(CONVERT_MAGIC_NO);

	/* iterate over the opts->flags */
	ret = _convert_write_tlvs(buff + length, buff_len - length, opts);
	if (ret < 0)
		return ret;

	length += (size_t)ret;
	hdr->total_length = BYTES_TO_CONVERT(length);

	return length;
}

struct convert_opts *read_convert_opts(int fd, bool peek, int *error_code, char *error_message)
{
	uint8_t hdr[CONVERT_HDR_LEN];
	int ret;
	int flag = peek ? MSG_PEEK : 0;
	size_t length;
	size_t offset = peek ? CONVERT_HDR_LEN : 0;
	struct convert_opts *opts;

	ret = recvfrom(fd, hdr, CONVERT_HDR_LEN, MSG_WAITALL | flag, NULL, NULL);

	if (ret < 0)
	{
		*error_code = errno;
		sprintf(error_message, "unable to read the convert header");
		return NULL;
	}

	if (convert_parse_header(hdr, ret, &length) < 0)
	{
		*error_code = errno;
		sprintf(error_message, "unable to parse the convert header");
		return NULL;
	}

	if (length)
	{
		uint8_t buffer[length + offset];

		/* if peek the data was not yet read, so we need to
		 * also read (again the main header). */
		if (peek)
			length += CONVERT_HDR_LEN;

		ret = recvfrom(fd, buffer, length, MSG_WAITALL, NULL, NULL);
		if (ret != (int)length || ret < 0)
		{
			*error_code = errno;
			sprintf(error_message, "unable to read the convert tlv data");
			return NULL;
		}

		opts = convert_parse_tlvs(buffer + offset, length - offset);
		if (opts == NULL)
		{
			// if opts is NULL it may be because of en empty buffer
			// TODO: check if this is the case
			return NULL;
		}

		/* if we receive the TLV error we need to inform the app */
		if (opts->flags & CONVERT_F_ERROR)
		{
			*error_code = (int)opts->error_code;
			sprintf(error_message, "received error from the convert server: %d", opts->error_code);
			convert_free_opts(opts);
			return NULL;
		}
	}
	*error_code = 0;

	return opts;
}