#include <crater/string.h>
#include <crater/slab.h>

void* memset(void* dest, int val, size_t count) {
	u8* d = dest;
	while (count--)
		*d++ = (u8)val;
	return dest;
}

void* memcpy(void* dest, const void* src, size_t count) {
	u8* d = dest;
	const u8* s = src;

	while (count--)
		*d++ = *s++;

	return dest;
}

void* memmove(void* dest, const void* src, size_t count) {
	u8* d = dest;
	const u8* s = src;

	if (d < s) {
		while (count--)
			*d++ = *s++;
	} else {
		d += count;
		s += count;

		while (count--)
			*--d = *--s;
	}

	return dest;
}

int memcmp(const void* b1, const void* b2, size_t count) {
	const u8* p1 = b1;
	const u8* p2 = b2;

	while (count--) {
		if (*p1 != *p2)
			return *p1 - *p2;
		p1++;
		p2++;
	}

	return 0;
}

void* memchr(const void* ptr, int val, size_t count) {
	const u8* p = ptr;
	while (count--) {
		if (*p == (u8)val)
			return (void*)p;
		p++;
	}
	return NULL;
}

size_t strlen(const char* str) {
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

char* strcpy(char* dest, const char* src) {
	char* d = dest;
	while ((*d++ = *src++) != '\0')
		/* Nothing */;
	return dest;
}

char* strncpy(char* dest, const char* src, size_t count) {
	char* d = dest;

	while (count) {
		if ((*d = *src) != '\0')
			src++;
		d++;
		count--;
	}

	return dest;
}

size_t strlcpy(char* dest, const char* src, size_t dsize) {
	size_t ret = strlen(src);
	if (dsize) {
		size_t count = ret >= dsize ? dsize - 1 : ret;
		memcpy(dest, src, count);
		dest[count] = '\0';
	}
	return ret;
}

char* strcat(char* dest, const char* src) {
	char* d = dest;
	while (*d)
		d++;
	while ((*d++ = *src++) != '\0')
		/* Nothing */;
	return dest;
}

char* strncat(char* dest, const char* src, size_t count) {
	char* d = dest;

	if (count) {
		while (*d)
			d++;
		while ((*d++ = *src++) != '\0') {
			if (--count == 0) {
				*d = '\0';
				break;
			}
		}
	}

	return dest;
}

size_t strlcat(char* dest, const char* src, size_t dsize) {
	size_t dlen = strlen(dest);
	size_t count = strlen(src);
	size_t ret = dlen + count;

	if (dlen >= dsize)
		return ret;

	dest += dlen;
	dsize -= dlen;

	if (count >= dsize)
		count = dsize - 1;
	memcpy(dest, src, count);
	dest[count] = '\0';

	return ret;
}

int strcmp(const char* s1, const char* s2) {
	unsigned char c1, c2;

	while (1) {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 != c2 || c1 == '\0')
			break;
	}

	return c1 - c2;
}

int strncmp(const char* s1, const char* s2, size_t count) {
	unsigned char c1 = '\0';
	unsigned char c2 = '\0';

	while (count--) {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 != c2 || c1 == '\0')
			break;
	}

	return c1 - c2;
}

char* strchr(const char* str, int c) {
	while (*str != (char)c) {
		if (*str == '\0')
			return NULL;
		str++;
	}
	return (char*)str;
}

char* strrchr(const char* str, int c) {
	const char* last = NULL;
	do {
		if (*str == (char)c)
			last = str;
	} while (*str++);
	return (char*)last;
}

char* strtok_r(char* str, const char* delim, char** saveptr) {
	char* ret;
	if (!str)
		str = *saveptr;
	while (*str && strchr(delim, *str))
		str++;
	if (*str == '\0')
		return NULL;

	ret = str;
	while (*str && !strchr(delim, *str))
		str++;
	if (*str)
		*str++ = '\0';
	*saveptr = str;
	return ret;
}

char* kstrdup(const char* str, mm_t mm_flags) {
	size_t size = strlen(str) + 1;
	char* copy = kmalloc(size, mm_flags);
	if (copy)
		memcpy(copy, str, size);
	return copy;
}
