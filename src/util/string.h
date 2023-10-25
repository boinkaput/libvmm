/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 * Copyright 2022, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// stole from musl

// static char *strcpy(char *dest, const char *src)
// {
// 	const unsigned char *s = (const unsigned char *) src;
// 	unsigned char *d = (unsigned char *) dest;
// 	while ((*d++ = *s++));
// 	return dest;
// }

static char *strncpy(char *dest, const char *src, size_t n)
{
	const unsigned char *s = (const unsigned char *) src;
	unsigned char *d = (unsigned char *) dest;
	for (; n && (*d=*s); n--, s++, d++);
	return dest;
}

static int strcmp(const char *l, const char *r)
{
	for (; *l==*r && *l; l++, r++);
	return *(unsigned char *)l - *(unsigned char *)r;
}

static int strncmp(const char *_l, const char *_r, size_t n)
{
	const unsigned char *l=(void *)_l, *r=(void *)_r;
	if (!n--) return 0;
	for (; *l && *r && n && *l == *r ; l++, r++, n--);
	return *l - *r;
}
