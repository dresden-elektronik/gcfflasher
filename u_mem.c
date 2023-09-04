

void U_memset(void *mem, int c, unsigned long n)
{
	unsigned char *m;

	c &= 0xFF;
	m = (unsigned char*)mem;

	for (;n; n--, m++)
		*m = (unsigned char)c;
}

void U_bzero(void *mem, unsigned long n)
{
	U_memset(mem, 0, n);
}

void *U_memcpy(void *dst, const void *src, unsigned long n)
{
	unsigned char *d;
	unsigned char *s;

	d = (unsigned char*)dst;
	s = (unsigned char*)src;

	for (;n; n--)
	{
		*d = *s;
		d++;
		s++;
	}

	return (void*)d;
}