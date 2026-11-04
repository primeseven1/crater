#include <crater/convert.h>

int kulltostr(char* dest, unsigned long long x, unsigned int base, size_t dsize) {
	if (dsize == 0)
		return -EOVERFLOW;
	*dest = '\0';
	if (dsize == 1)
		return -EOVERFLOW;
	const char* digits = "0123456789abcdef";
	if (base < 2 || base > __builtin_strlen(digits))
		return -EINVAL;

	/* Do the actual conversion */
	char* d = dest;
	do {
		if (dsize == 1)
			break;
		*d++ = digits[x % base];
		x /= base;
		dsize--;
	} while (x);
	*d = '\0';

	/* Make sure the conversion wasn't cut off by dsize */
	if (x)
		return -EOVERFLOW;

	/* Now reverse the string */
	d--;
	while (dest < d) {
		char tmp = *dest;
		*dest++ = *d;
		*d-- = tmp;
	}

	return 0;
}

int klltostr(char* dest, long long x, unsigned int base, size_t dsize) {
	if (x < 0) {
		if (dsize == 0)
			return -EOVERFLOW;
		if (--dsize == 0) {
			*dest = '\0';
			return -EOVERFLOW;
		}
		*dest++ = '-';
		if (x != LLONG_MIN)
			x = -x;
	}

	return kulltostr(dest, (unsigned long long)x, base, dsize);
}

static inline bool isdigit(char c) {
	return (c >= '0' && c <= '9');
}

static inline bool isxdigit(char c) {
	return (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

static inline char tolower(char c) {
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static inline bool isspace(char c) {
	return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
}

int kstrtoull(const char* str, unsigned int base, unsigned long long* res) {
	if (!str)
		return -EINVAL;

	while (isspace(*str))
		str++;
	if (!*str || *str == '-')
		return -EINVAL;
	if (*str == '+')
		str++;

	if (base == 0) {
		if (*str == '0') {
			if (str[1] == 'x' || str[1] == 'X') {
				base = 16;
				str += 2;
			} else {
				base = 8;
				str++;
			}
		} else {
			base = 10;
		}
	}

	if (base < 2 || base > 16 || !*str)
		return -EINVAL;

	unsigned long long x = 0;
	for (; *str; str++) {
		int digit;
		if (isdigit(*str))
			digit = *str - '0';
		else if (isxdigit(*str))
			digit = (tolower(*str) - 'a') + 10;
		else
			return -EINVAL;

		if (digit >= (int)base)
			return -EINVAL;
		if (x > (ULLONG_MAX - digit) / (unsigned long long)base)
			return -ERANGE;
		x = x * base + digit;
	}

	*res = x;
	return 0;
}

int kstrtoll(const char* str, unsigned int base, long long* res) {
	if (!str)
		return -EINVAL;
	while (isspace(*str))
		str++;
	if (!*str)
		return -EINVAL;

	int neg = 0;
	if (*str == '-') {
		neg = 1;
		str++;
	} else if (*str == '+') {
		str++;
	}

	unsigned long long tmp;
	int ret = kstrtoull(str, base, &tmp);
	if (ret)
		return ret;

	if (neg) {
		if (tmp > (unsigned long long)LLONG_MAX + 1ull)
			return -ERANGE;
		*res = -(long long)tmp;
	} else {
		if (tmp > (unsigned long long)LLONG_MAX)
			return -ERANGE;
		*res = (long long)tmp;
	}

	return 0;
}
